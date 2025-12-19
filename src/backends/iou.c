// TODO: verify iel_arg_un flags is empty
// TODO: return handle or error to allow cancelling
// TODO: queue
#define _DEFAULT_SOURCE
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <linux/prctl.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <errno.h>
#include <string.h>

#include <linux/io_uring.h>

#include <iel/arg.h>
#include <iel/backends.h>
#include <iel/backends/iou.h>
#include <iel_priv/quep.h>

#define IEL_QUE_IMPL
#define IEL_QUE_TPL (/*ChunkEntsBit=*/6, /*MinChunks=*/8, /*type=*/struct io_uring_sqe, /*pfx=*/ielb_ioux_xsq_, /*sfx=*/, /*api=*/static inline)
#include <iel_priv/que.tpl.h>

static inline
void a16from64(unsigned long long i, char str[16]) {
    const char hexa[] = "0123456789abcdef";
    unsigned char j = 16;
    while (j != 0) {
        str[--j] = hexa[i & 0xF];
        i >>= 4;
    }
}

static inline
void pu64hx(const char *hint, size_t hintlen, unsigned long long i, char e) {
    if (hint) {
        write(STDERR_FILENO, hint, hintlen);
        write(STDERR_FILENO, ": ", 2);
    }
    char str[19];
    str[0] = '0';
    str[1] = 'x';
    str[18] = e;
    a16from64(i, &str[2]);
    write(STDERR_FILENO, str, 19);
}

#define QUEUE_DEPTH 64
#define PTR_OFFSET_CAST(voidp, os, typ) ((typ *)((unsigned char *)voidp + os))
#define PTR_OFFSET(voidp, os) PTR_OFFSET_CAST(voidp, os, void)

#define ld_rlx(atomic_p) atomic_load_explicit(atomic_p, memory_order_relaxed)
#define ld_acq(atomic_p) atomic_load_explicit(atomic_p, memory_order_acquire)
#define st_rlx(atomic_p, new_val) atomic_store_explicit(atomic_p, new_val, memory_order_relaxed)
#define st_rel(atomic_p, new_val) atomic_store_explicit(atomic_p, new_val, memory_order_release)

// read mostly
struct iel_iou_ctx_st {
    // ----- hot -----
    // Submission ring pointers
    unsigned _Atomic *sring_tail;  // R,W; release
    unsigned _Atomic const *sring_head;  // R; acquire
    unsigned *sring_array;  // W+os
    struct io_uring_sqe *mapptr_sqes;  // W+os
    // Completion ring pointers
    unsigned _Atomic *cring_head;  // R,W; release
    unsigned _Atomic const *cring_tail;  // R; acquire
    struct io_uring_cqe const *cqes;  // R+os
    // other data
    unsigned cring_mask;
    unsigned sring_mask;
    // ----- CACHELINE -----
    struct iel_que_st taskque;
    // ----- cold(SQPOLL) -----
    int ring_fd;
    // ----- cold(NORM) -----
    unsigned int maplen_sqes;
    unsigned int maplen_sq;
    unsigned int maplen_cq;
    unsigned long long feat;
    void *mapptr_sq;
    void *mapptr_cq;
};
// #define PEXPR(x) static char (*__kaboom ## __COUNTER__)[x] = 1;
// PEXPR(sizeof(struct iel_iou_ctx_st))

/*
* System call wrappers provided since glibc does not yet
* provide wrappers for io_uring system calls.
* */

static inline
int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
    int ret = syscall(__NR_io_uring_setup, entries, p);
    return (ret < 0) ? -errno : ret;
}

static inline
int io_uring_enter(int ring_fd, unsigned int to_submit,
                   unsigned int min_complete, unsigned int flags)
{
    int ret = syscall(__NR_io_uring_enter, ring_fd, to_submit,
                  min_complete, flags, NULL, 0);
    return (ret < 0) ? -errno : ret;
}

/*
* Submit a read or a write request to the submission queue.
* */
static inline
void submit_to_sq(struct iel_iou_ctx_st *pud, unsigned char op, int fd, long long off, unsigned long long addr, unsigned int len, iel_cbp cbp) {
    // TODO: handle CQ overflow/-EBUSY? see IORING_FEAT_NODROP in io_uring_setup(2)
    unsigned index, tail;

    /* Add our submission queue entry to the tail of the SQE ring buffer */
    tail = ld_rlx(pud->sring_tail);
    unsigned sq_ents = (unsigned)(tail - ld_acq(pud->sring_head));
    if (sq_ents == pud->sring_mask + 1) {  // maybe replace with actual size? should be equivalent though
        fprintf(stderr, "unexpected sq full!\n");
        abort();
    } else
        fprintf(stderr, "pushing event into sq, current size: %u\n", sq_ents);
    index = tail & pud->sring_mask;
    struct io_uring_sqe *sqe = &pud->mapptr_sqes[index];
    /* Fill in the parameters required );for the read or write operation */
    sqe->opcode = op;
    sqe->fd = fd;
    sqe->addr = addr;
    sqe->len = len;
    sqe->off = (unsigned long long)off;
    sqe->user_data = (unsigned long long)cbp;

    pud->sring_array[index] = index;

    /* Update the tail */
    st_rel(pud->sring_tail, tail + 1);
}


void ielb_iou_fpr(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    submit_to_sq((struct iel_iou_ctx_st *)ctx, IORING_OP_READ, fd, offset, (unsigned long long)buf, count, cbp);
}
void ielb_iou_fprv(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovlen, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    submit_to_sq((struct iel_iou_ctx_st *)ctx, IORING_OP_READV, fd, offset, (unsigned long long)iovecs, iovlen, cbp);
}
void ielb_iou_fpw(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    submit_to_sq((struct iel_iou_ctx_st *)ctx, IORING_OP_WRITE, fd, offset, (unsigned long long)buf, count, cbp);
}
void ielb_iou_fpwv(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovlen, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    submit_to_sq((struct iel_iou_ctx_st *)ctx, IORING_OP_WRITEV, fd, offset, (unsigned long long)iovecs, iovlen, cbp);
}

void ielb_ioux_r(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    submit_to_sq((struct iel_iou_ctx_st *)ctx, IORING_OP_READ, fd, (unsigned long long)-1, (unsigned long long)buf, count, cbp);
}
void ielb_ioux_rv(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    submit_to_sq((struct iel_iou_ctx_st *)ctx, IORING_OP_READV, fd, (unsigned long long)-1, (unsigned long long)iov, iovcnt, cbp);
}
void ielb_ioux_w(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    submit_to_sq((struct iel_iou_ctx_st *)ctx, IORING_OP_WRITE, fd, (unsigned long long)-1, (unsigned long long)buf, count, cbp);
}
void ielb_ioux_wv(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    submit_to_sq((struct iel_iou_ctx_st *)ctx, IORING_OP_WRITEV, fd, (unsigned long long)-1, (unsigned long long)iov, iovcnt, cbp);
}

struct ielb_ioux_etime_cbt {
    struct iel_cb_base base;
    struct timespec *ts;
    iel_cbp cbp;
};

static
void ielb_ioux_etime_cb(iel_cbp _self, int res) {
    struct ielb_ioux_etime_cbt *cbp = (struct ielb_ioux_etime_cbt *)_self;
    iel_cbp cbp_inner = cbp->cbp;

    free(cbp->ts);
    free(cbp);
    cbp_inner->cb(cbp_inner, res == -ETIME ? 0 : res);
}

void ielb_iou_etime(void *ctx, unsigned long long time, union iel_arg_un flags, iel_cbp cbp) {
    struct iel_iou_ctx_st *pud = (struct iel_iou_ctx_st *)ctx;
    struct timespec *pts = malloc(sizeof(struct timespec));
    if (flags.ull & IEL_FLAG_ETIME_MICROS) {
        flags.ull &= ~IEL_FLAG_ETIME_MICROS;
        pts->tv_sec = time / 1000000;
        pts->tv_nsec = (time % 1000000) * 1000;
    } else {
        pts->tv_sec = time / 1000;
        pts->tv_nsec = (time % 1000) * 1000000;
    }
    if (flags.ull) {
        cbp->cb(cbp, -EINVAL);
        free(pts);
        return;
    }
    {
        struct ielb_ioux_etime_cbt *wcbp = malloc(sizeof(struct ielb_ioux_etime_cbt));

        wcbp->base.cb = ielb_ioux_etime_cb;
        wcbp->ts = pts;
        wcbp->cbp = cbp;

        submit_to_sq(pud, IORING_OP_TIMEOUT, 0, 0, (unsigned long long) pts, 1, &wcbp->base);
    }
}

void ielb_iou_esoon(void *ctx, union iel_arg_un flags, iel_cbp cbp) {
    (void) flags;
    struct iel_iou_ctx_st *pud = (struct iel_iou_ctx_st *)ctx;
    iel_quep_push1(&pud->taskque, cbp);
}

static inline
void ielb_ioux_drainque(struct iel_que_st *que) {
    int queres;
    iel_cbp task;
    while (1) {
        queres = iel_quep_pop1(que, (void **)&task);
        if (queres < 0) break;
        task->cb(task, 0);
    }
    iel_quep_qtrim(que);
}

int ielb_iou_lrun1(void *ctx, union iel_arg_un flags) {
    (void) flags;
    struct iel_iou_ctx_st *pud = (struct iel_iou_ctx_st *)ctx;
    unsigned head;
    unsigned sq_len;
    ielb_ioux_drainque(&pud->taskque);

    // relaxed because no synchronisation needed
    sq_len = ld_rlx(pud->sring_tail) - ld_rlx(pud->sring_head);
    fprintf(stderr, "awaiting submission of %u SQEs and completion of 1 event\n", sq_len);
    /*
    * Tell the kernel we have submitted events with the io_uring_enter()
    * system call. We also pass in the IORING_ENTER_GETEVENTS flag which
    * causes the io_uring_enter() call to wait until min_complete
    * (the 3rd param) events complete.
    * */
    int ret = io_uring_enter(pud->ring_fd, sq_len, 1,
                              IORING_ENTER_GETEVENTS);
    if (ret < 0) {
        perror("io_uring_enter");
        return ret;
    }

    head = ld_rlx(pud->cring_head);
    fprintf(stderr, "reading from cq %u entries\n", ld_acq(pud->cring_tail) - head);

    /*
    * Remember, this is a ring buffer. If head == tail, it means that the
    * buffer is empty.
    * */
    while (head != ld_acq(pud->cring_tail)) {
        /* Get the entry */
        struct io_uring_cqe const *cqe = &pud->cqes[head & pud->cring_mask];

        iel_cbp cbp = (iel_cbp)cqe->user_data;
        if (cbp)
            cbp->cb(cbp, cqe->res);

        /* Write barrier so that update to the head are made visible */
        st_rel(pud->cring_head, ++head);

        if (head == ld_acq(pud->cring_tail)) break;

        ielb_ioux_drainque(&pud->taskque);
    }
    return 0;
}
size_t ielb_iou_lsize(void) {
    return sizeof(struct iel_iou_ctx_st);
}

static
volatile sig_atomic_t setup_trapped = 0;

int ielb_ioux_lnew_us(void *ctx, union iel_arg_un flags) {
    (void) flags;
    struct iel_iou_ctx_st *pud = (struct iel_iou_ctx_st *)ctx;
    struct io_uring_params p;

    /* See io_uring_setup(2) for io_uring_params.flags you can set */
    memset(&p, 0, sizeof(p));
    setup_trapped = 0;
    pud->ring_fd = io_uring_setup(QUEUE_DEPTH, &p);
    if (setup_trapped) {
        perror("io_uring_setup (blocked by SECCOMP_RET_TRAP)");
        goto fail;
    }
    if (pud->ring_fd < 0) {
        perror("io_uring_setup");
        goto fail;
    } else
        fprintf(stderr, "io_uring_setup returned %d\n", pud->ring_fd);
    if (!(p.features & (IORING_FEAT_NODROP | IORING_FEAT_RW_CUR_POS)))
        goto fail_closefd;

    /*
     * io_uring communication happens via 2 shared kernel-user space ring
     * buffers, which can be jointly mapped with a single mmap() call in
     * kernels >= 5.4.
     */

    // TODO: support SQE128?
    // TODO: SQPOLL
    pud->maplen_sqes = p.sq_entries * sizeof(struct io_uring_sqe);
    int sring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    int cring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);

    /* Rather than check for kernel version, the recommended way is to
     * check the features field of the io_uring_params structure, which is a
     * bitmask. If IORING_FEAT_SINGLE_MMAP is set, we can do away with the
     * second mmap() call to map in the completion ring separately.
     */
    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        // cring_sz = sring_sz = max(cring_sz, sring_sz)
        if (cring_sz > sring_sz)
            sring_sz = cring_sz;
        cring_sz = sring_sz;
    }
    pud->maplen_cq = cring_sz;
    pud->maplen_sq = sring_sz;

    /* Map in the submission and completion queue ring buffers.
     *  Kernels < 5.4 only map in the submission queue, though.
     */
    pud->mapptr_sq = mmap(0, sring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE,
                  pud->ring_fd, IORING_OFF_SQ_RING);
    if (pud->mapptr_sq == MAP_FAILED) {
        perror("mmap");
        goto fail_closefd;
    }

    if (p.features & IORING_FEAT_SINGLE_MMAP) {
        pud->mapptr_cq = pud->mapptr_sq;
    } else {
        /* Map in the completion queue ring buffer in older kernels separately */
        pud->mapptr_cq = mmap(0, cring_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE,
                      pud->ring_fd, IORING_OFF_CQ_RING);
        if (pud->mapptr_cq == MAP_FAILED) {
            perror("mmap");
            goto fail_unmapsq;
        }
    }

    /* Save useful fields for later easy reference */
    pud->sring_head = PTR_OFFSET(pud->mapptr_sq, p.sq_off.head);
    pud->sring_tail = PTR_OFFSET(pud->mapptr_sq, p.sq_off.tail);
    pud->sring_mask = *PTR_OFFSET_CAST(pud->mapptr_sq, p.sq_off.ring_mask, unsigned);
    pud->sring_array = PTR_OFFSET(pud->mapptr_sq, p.sq_off.array);

    /* Map in the submission queue entries array */
    pud->mapptr_sqes = mmap(0, pud->maplen_sqes,
                   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                   pud->ring_fd, IORING_OFF_SQES);
    if (pud->mapptr_sqes == MAP_FAILED) {
        perror("mmap");
        goto fail_unmapcq;
    }

    /* Save useful fields for later easy reference */
    pud->cring_head = PTR_OFFSET(pud->mapptr_cq, p.cq_off.head);
    pud->cring_tail = PTR_OFFSET(pud->mapptr_cq, p.cq_off.tail);
    pud->cring_mask = *PTR_OFFSET_CAST(pud->mapptr_cq, p.cq_off.ring_mask, unsigned);
    pud->cqes = PTR_OFFSET(pud->mapptr_cq, p.cq_off.cqes);

    if (iel_quep_init(&pud->taskque, 0) < 0) {
        goto fail_unmapsqes;
    }

    pud->feat = IEL_FEAT_AVAIL | IEL_FEAT_ETIME_MICROS;

    return 0;
fail_unmapsqes:;
    munmap(pud->mapptr_sqes, pud->maplen_sqes);
fail_unmapcq:;
    if (!(p.features & IORING_FEAT_SINGLE_MMAP))
        munmap(pud->mapptr_cq, pud->maplen_cq);
fail_unmapsq:;
    munmap(pud->mapptr_sq, pud->maplen_sq);
fail_closefd:;
    close(pud->ring_fd);
fail:;
    return 1;
}
static
void sigactcb(int sig, siginfo_t *si, void *ctx) {
    (void) ctx;
    setup_trapped = 1;
    errno = si->si_errno;
#define P1(x) write(STDERR_FILENO, x, sizeof(x) - 1)
#define P(x) pu64hx(#x, sizeof(#x) - 1, (unsigned long long)si->si_##x, '\n')
    P1("oneshot SIGSYS handler\n");
    pu64hx("_sig", 4, sig, '\n');
    P(signo);
    P(errno);
    P(code);
    P(status);
    P(value.sival_int);
    P(int);
    P(ptr);
    P(addr);
    P(fd);
    P(call_addr);
    P(syscall);
    P(arch);
#undef P
#undef P1
}
struct ielb_ioux_lnewt_arg {
    void *ctx;
    int res;
};
static
void *ielb_ioux_lnewt_cb(void *_arg) {
    struct ielb_ioux_lnewt_arg *arg = _arg;
    struct sigaction sa = {
        .sa_sigaction = sigactcb,
        .sa_flags = SA_SIGINFO | SA_RESETHAND,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSYS, &sa, NULL);
    int r = ielb_ioux_lnew_us(arg->ctx, IEL_ARG_NULL);
    if (r)
        fprintf(stderr, "erret: %d\n", r);
    arg->res = r;
    return NULL;
}
int ielb_iou_lnew(void *ctx, union iel_arg_un flags) {
    (void) flags;
    pthread_t pth;
    int res;
    struct ielb_ioux_lnewt_arg arg = { .ctx=ctx, .res=255 };
    void *tres;

    res = pthread_create(&pth, NULL, ielb_ioux_lnewt_cb, &arg);
    if (res) {
        fprintf(stderr, "pthread_create: %s\n", strerror(res));
        return 4;
    }
    res = pthread_join(pth, &tres);
    if (res) {
        fprintf(stderr, "pthread_join: %s\n", strerror(res));
        return 3;
    }
    return arg.res;
}
void ielb_iou_ldel(void *ctx) {
    struct iel_iou_ctx_st *pud = (struct iel_iou_ctx_st *)ctx;
    iel_quep_del(&pud->taskque);
    munmap(pud->mapptr_sqes, pud->maplen_sqes);
    munmap(pud->mapptr_sq, pud->maplen_sq);
    if (pud->mapptr_cq != pud->mapptr_sq)
        munmap(pud->mapptr_cq, pud->maplen_cq);
    close(pud->ring_fd);
}
union iel_arg_un ielb_iou_xcntl(void *ctx, unsigned short op, union iel_arg_un arg0, union iel_arg_un arg1) {
    (void) arg0;
    (void) arg1;

    struct iel_iou_ctx_st *pud = (struct iel_iou_ctx_st *)ctx;
    switch (op) {
        case IELB_IOU_XCNTL_SQLEN:
            return (union iel_arg_un) { .ull = (unsigned)(ld_rlx(pud->sring_tail) - ld_rlx(pud->sring_head)) };
        default:
            return (union iel_arg_un) { .ull = (unsigned long long) -1 };
    }
}

unsigned long long ielb_iou_cap = IEL_FEAT_AVAIL | IEL_FEAT_ETIME_MICROS;

unsigned long long ielb_iou_xfeat(void *ctx, union iel_arg_un flags) {
    (void) flags;
    struct iel_iou_ctx_st *pud = (struct iel_iou_ctx_st *)ctx;
    if (!ctx) return 0;
    return pud->feat;
}

void (ielb_ioux_nop_a)(union iel_arg_un flags) {
    (void) flags;
}

unsigned char ielb_iou_vtsetup(struct iel_vtable_st *vt) {
#ifndef __linux__
    (void) vt;
    return IEL_VTSETUP_RET_UNAVAIL;
#else
    if (!vt) {
        errno = EINVAL;
        return IEL_VTSETUP_RET_ERROR;
    }
#define SETUP_VTABLE_FN(x, y) vt->p_##x = ielb_iou_##y
#define SETUP_VTABLE_NAME(x) SETUP_VTABLE_FN(x,x)
    SETUP_VTABLE_NAME(fpr);
    SETUP_VTABLE_NAME(fprv);
    SETUP_VTABLE_NAME(fpw);
    SETUP_VTABLE_NAME(fpwv);

    SETUP_VTABLE_NAME(fr);
    SETUP_VTABLE_NAME(frv);
    SETUP_VTABLE_NAME(fw);
    SETUP_VTABLE_NAME(fwv);

    SETUP_VTABLE_NAME(sr);
    SETUP_VTABLE_NAME(srv);
    SETUP_VTABLE_NAME(sw);
    SETUP_VTABLE_NAME(swv);

    SETUP_VTABLE_NAME(etime);
    SETUP_VTABLE_NAME(esoon);

    SETUP_VTABLE_NAME(ldel);
    SETUP_VTABLE_NAME(lrun1);
    SETUP_VTABLE_NAME(lsize);

    SETUP_VTABLE_NAME(xfeat);
    SETUP_VTABLE_NAME(xcntl);

    SETUP_VTABLE_NAME(xinit);
    SETUP_VTABLE_NAME(xtdwn);

    int scmode = prctl(PR_GET_SECCOMP);
    // scmode won't be SECCOMP_MODE_STRICT since prctl would be blocked in that case
    if (scmode == SECCOMP_MODE_DISABLED) {
        vt->p_lnew = ielb_ioux_lnew_us;
    } else {
        fputs("SECCOMP_MODE_FILTER\n", stderr);
        SETUP_VTABLE_NAME(lnew);
        // Android (app) uses SECCOMP_RET_TRAP (syscall ret 64), and docker uses SECCOMP_RET_ERRNO | EPERM (or lsm?)
        // doesn't work for SECCOMP_RET_KILL_PROCESS
        // Note: the use of SECCOMP_RET_KILL_THREAD to kill a single thread in a multithreaded process is likely to leave the process in a permanently inconsistent and possibly corrupt state.

    }
#undef SETUP_VTABLE_NAME
#undef SETUP_VTABLE_FN

    return IEL_VTSETUP_RET_UNSURE;
#endif /* ifndef __linux__ */
}
