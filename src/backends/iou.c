// TODO: verify iel_arg_un flags is empty and return EINVAL when invalid
// XXX: known limitations: IOSQE_IO_LINK maximum batch size = 8
// XXX: never use IOSQE_IO_HARDLINK without IOSQE_IO_LINK, or
// any flag that implies IOSQE_IO_LINK
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <errno.h>
#include <string.h>

#include <linux/io_uring.h>

#include <iel/arg.h>
#include <iel/backends.h>
#include <iel/backends/iou.h>
#include <iel/tagptr.h>
#include <iel/errno.h>
#include <iel_priv/quep.h>

#define SQOFQ_CHUNK_SZ_BIT 10
static_assert(SQOFQ_CHUNK_SZ_BIT > 3, "Chunk too small");
#define IEL_QUE_IMPL
#define IEL_QUE_TPL (/*ChunkEntsBit=*/SQOFQ_CHUNK_SZ_BIT, /*MinChunks=*/8, /*type=*/struct io_uring_sqe, /*pfx=*/ielb_ioux_xsq_, /*sfx=*/, /*api=*/static inline, /*align=*/64,)
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

// Non-SQPOLL saturates at 64, 128 is better for SQPOLL
#define QUEUE_DEPTH_BIT 6
static_assert(QUEUE_DEPTH_BIT > 3, "Queue too small");
#define QUEUE_DEPTH (1ULL << QUEUE_DEPTH_BIT)
#define PTR_OFFSET(voidp, os, typ) ((typ)((unsigned char *)voidp + os))

#define ld_rlx(atomic_p) atomic_load_explicit(atomic_p, memory_order_relaxed)
#define ld_acq(atomic_p) atomic_load_explicit(atomic_p, memory_order_acquire)
#define st_rlx(atomic_p, new_val) atomic_store_explicit(atomic_p, new_val, memory_order_relaxed)
#define st_rel(atomic_p, new_val) atomic_store_explicit(atomic_p, new_val, memory_order_release)

size_t (ielb_iou_lsize)(void) {
    return ielb_iou_lsize();  // use the macro
}

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
void *submit_to_sq(struct ielb_iou_ctx_st *pud, unsigned char op, int fd, long long off, unsigned long long addr, unsigned int len, void *user_data) {
    // TODO: handle CQ overflow/-EBUSY? see IORING_FEAT_NODROP in io_uring_setup(2)
    unsigned tail = pud->lcl_stail;
    unsigned sq_ents = (unsigned)(tail - ld_acq(pud->sring_head));
    struct io_uring_sqe *sqe;
    if (sq_ents == pud->sring_mask + 1) {  // SQ full
        sqe = ielb_ioux_xsq_rsv1(&pud->sqofq);
        if (!sqe) {
            iel_cb cb = *(iel_cb *)iel_tp_untag(user_data, IEL_CB_ALIGN).ptr;

            cb(user_data, -ENOMEM);
            iel_errno = IEL_ENOMEM;
            return NULL;
        }
        fprintf(stderr, "sq full!\n");
    } else {
        unsigned index;
        fprintf(stderr, "pushing event into sq, current size: %u\n", sq_ents);
        index = tail & pud->sring_mask;
        /* Add our submission queue entry to the tail of the SQE ring buffer */
        sqe = &pud->mapptr_sqes[index];
        pud->sring_array[index] = index;
        /* Update the tail */
        ++pud->lcl_stail;
    }
    /* Fill in the parameters required for the read or write operation */
    memset(sqe, 0, 64);
    sqe->flags = 0;
    sqe->opcode = op;
    sqe->fd = fd;
    sqe->addr = addr;
    sqe->len = len;
    sqe->off = (unsigned long long)off;
    sqe->user_data = (unsigned long long)user_data;
    // TODO: flush immediately if SQPOLL(?)

    return user_data;
}


void *ielb_iou_fpr(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, void *cbp) {
    (void) flags;
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_READ, fd, offset, (unsigned long long)buf, count, cbp);
}
void *ielb_iou_fprv(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovcnt, iel_pf_pos offset, union iel_arg_un flags, void *cbp) {
    (void) flags;
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_READV, fd, offset, (unsigned long long)iovecs, iovcnt, cbp);
}
void *ielb_iou_fpw(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, void *cbp) {
    (void) flags;
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_WRITE, fd, offset, (unsigned long long)buf, count, cbp);
}
void *ielb_iou_fpwv(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovcnt, iel_pf_pos offset, union iel_arg_un flags, void *cbp) {
    (void) flags;
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_WRITEV, fd, offset, (unsigned long long)iovecs, iovcnt, cbp);
}

void *ielb_ioux_r(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp) {
    (void) flags;
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_READ, fd, (unsigned long long)-1, (unsigned long long)buf, count, cbp);
}
void *ielb_ioux_rv(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp) {
    (void) flags;
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_READV, fd, (unsigned long long)-1, (unsigned long long)iov, iovcnt, cbp);
}
void *ielb_ioux_w(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp) {
    (void) flags;
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_WRITE, fd, (unsigned long long)-1, (unsigned long long)buf, count, cbp);
}
void *ielb_ioux_wv(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp) {
    (void) flags;
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_WRITEV, fd, (unsigned long long)-1, (unsigned long long)iov, iovcnt, cbp);
}

struct ielb_ioux_etime_cbt {
    IEL_CB_BASE base;
    struct timespec ts;
    void *user_data;
};

static
void ielb_ioux_etime_cb(void *_self, int res) {
    iel_cb cb_inner;
    void *user_data;
    {
        struct ielb_ioux_etime_cbt *cbp = (struct ielb_ioux_etime_cbt *)iel_tp_untag(_self, IEL_CB_ALIGN).ptr;
        user_data = cbp->user_data;

        cb_inner = *(iel_cb *)iel_tp_untag(user_data, IEL_CB_ALIGN).ptr;

        free(cbp);
    }
    cb_inner(user_data, res == -ETIME ? 0 : res);
}

void *ielb_iou_etime(void *ctx, unsigned long long time, union iel_arg_un flags, void *user_data) {
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    struct ielb_ioux_etime_cbt *wcbp = (struct ielb_ioux_etime_cbt *)malloc(sizeof(struct ielb_ioux_etime_cbt));
    if (!wcbp) {
        iel_cb cb = *(iel_cb *)iel_tp_untag(user_data, IEL_CB_ALIGN).ptr;
        cb(user_data, -ENOMEM);
        iel_errno = IEL_ENOMEM;
        return NULL;
    }
    if (flags.ull & IEL_FLAG_ETIME_MICROS) {
        flags.ull &= ~IEL_FLAG_ETIME_MICROS;
        wcbp->ts.tv_sec = time / 1000000;
        wcbp->ts.tv_nsec = (time % 1000000) * 1000;
    } else {
        wcbp->ts.tv_sec = time / 1000;
        wcbp->ts.tv_nsec = (time % 1000) * 1000000;
    }
    if (flags.ull) {
        iel_cb cb;

        free(wcbp);
        cb = *(iel_cb *)iel_tp_untag(user_data, IEL_CB_ALIGN).ptr;
        cb(user_data, -EINVAL);
        iel_errno = IEL_EINVAL;
        return NULL;
    }
    wcbp->base = &ielb_ioux_etime_cb;
    wcbp->user_data = user_data;

    return submit_to_sq(pud, IORING_OP_TIMEOUT, 0, 0, (unsigned long long)&wcbp->ts, 1, IEL_TAGCB(&wcbp->base));
}

void *ielb_iou_esoon(void *ctx, union iel_arg_un flags, void *user_data) {
    (void) flags;
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
#if 0
    void **pout = iel_quep_rsv1(&pud->taskque);
    if (!pout) {
        iel_cbp cbp = (iel_cbp)iel_tp_untag(user_data, IEL_CB_ALIGN).ptr;

        cbp->cb(user_data, -ENOMEM);
        iel_errno = IEL_ENOMEM;
        return NULL;
    }
    *pout = user_data;
    return user_data;
#else
    return submit_to_sq(pud, IORING_OP_NOP, 0, 0, 0, 0, user_data);
#endif
}

static inline
void ielb_ioux_drainque(struct iel_que_st *que) {
    void *task;
    while (1) {
        if (iel_quep_pop1(que, &task) < 0) break;
        iel_cb cb = *(iel_cb *)iel_tp_untag(task, IEL_CB_ALIGN).ptr;
        cb(task, 0);
    }
    iel_quep_qtrim(que);
}

int ielb_iou_lrun1(void *ctx, union iel_arg_un flags) {
    (void) flags;
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    unsigned sq_len;
    ielb_ioux_drainque(&pud->taskque);

    // relaxed as no synchronisation required
    sq_len = pud->lcl_stail - ld_rlx(pud->sring_head);
    fprintf(stderr, "awaiting submission of %u SQEs and completion of 1 event; submitted: [\n", sq_len);
    for (unsigned idx = ld_acq(pud->sring_head); idx < pud->lcl_stail; ++idx) {
        unsigned real_idx = pud->sring_array[idx & pud->sring_mask];
        struct io_uring_sqe *s = &pud->mapptr_sqes[real_idx];
        fprintf(stderr, "  {OP=%hu,\tUD=%p}@%p\n", s->opcode, (void *)s->user_data, (void *)s);
    }
    fprintf(stderr, "]\n");
    // TODO: submission indirect arguments could be put into an arena, freed when submitted
    // e.g. struct timespec in the timer
    /*
     * Tell the kernel we have submitted events with the io_uring_enter()
     * system call. We also pass in the IORING_ENTER_GETEVENTS flag which
     * causes the io_uring_enter() call to wait until min_complete
     * (the 3rd param) events complete.
     */
    st_rel(pud->sring_tail, pud->lcl_stail);
    st_rel(pud->cring_head, pud->lcl_chead);
    int ret = io_uring_enter(pud->ring_fd, sq_len, 1,
                              IORING_ENTER_GETEVENTS);
    if (ret < 0) {
        perror("io_uring_enter");
        return ret;
    }

    size_t xsq_sz = ielb_ioux_xsq_size(&pud->sqofq);
    fprintf(stderr, "XSQ has %zu; reading from cq %u entries\n", xsq_sz, ld_acq(pud->cring_tail) - pud->lcl_chead);
    if (xsq_sz) {
        unsigned safe_popsz;
        {
            struct io_uring_sqe *current_chunk;
            size_t chunk_tail, os_tail;
            size_t pop_maxidx = xsq_sz - 1;
            if (pud->sring_mask < pop_maxidx)
                pop_maxidx = pud->sring_mask;

            os_tail = pud->sqofq.os_s + pop_maxidx;
            chunk_tail = pud->sqofq.chunk_s + (os_tail >> SQOFQ_CHUNK_SZ_BIT);
            os_tail &= ((1ULL << SQOFQ_CHUNK_SZ_BIT) - 1);

            current_chunk = ((struct io_uring_sqe **)pud->sqofq.map)[chunk_tail];
            while (1) {
                unsigned char flags = current_chunk[os_tail].flags;
                if (!(flags & IOSQE_IO_LINK))
                    break;

                if (!os_tail--)
                    current_chunk = ((struct io_uring_sqe **)pud->sqofq.map)[--chunk_tail];
                os_tail &= ((1ULL << SQOFQ_CHUNK_SZ_BIT) - 1);
            };
            safe_popsz = (unsigned) (
                ((chunk_tail - pud->sqofq.chunk_s) << SQOFQ_CHUNK_SZ_BIT)
                + os_tail - pud->sqofq.os_s + 1);
        }
        {
            printf("before: (struct iel_que_st) { .map=%p, .mapcap=%zu, .chunk_s=%zu, .os_s=%hu, .chunk_e=%zu, .os_e=%hu }\nmap: [", pud->sqofq.map, pud->sqofq.mapcap, pud->sqofq.chunk_s, pud->sqofq.os_s, pud->sqofq.chunk_e, pud->sqofq.os_e);
            for (iel_que_sz i = 0; i < pud->sqofq.mapcap; ++i) {
                printf("%p; ", (void *)(((void ***)pud->sqofq.map)[i]));
            }
            puts("]");
            printf("que: [");
            iel_que_idx it_ch = pud->sqofq.chunk_s;
            iel_que_offset it_os = pud->sqofq.os_s;
            while (it_ch != pud->sqofq.chunk_e || it_os != pud->sqofq.os_e) {
                struct io_uring_sqe *s = &((struct io_uring_sqe **)pud->sqofq.map)[it_ch][it_os];
                printf("{OP=%hu,UD=%p}; ", s->opcode, (void *)s->user_data);
                it_os++;
                it_os &= ((1ULL << SQOFQ_CHUNK_SZ_BIT) - 1);
                if (!it_os) ++it_ch;
            }
            puts("]");
        }
        {
            unsigned idx = pud->lcl_stail, loop_end = idx + safe_popsz;
            do {
                unsigned arr_idx = idx & pud->sring_mask;
                pud->sring_array[arr_idx] = arr_idx;
            } while (++idx != loop_end);
            // TODO: pop to sqes[0], and fill indirection array accordingly
        }
        {
            struct io_uring_sqe *arr_out;
            unsigned arr_sz;
            unsigned idx_begin;

            idx_begin = pud->lcl_stail & pud->sring_mask;
            pud->lcl_stail += safe_popsz;

            arr_out = &pud->mapptr_sqes[idx_begin];
            /* maximum contiguous writable size */
            arr_sz = (pud->sring_mask + 1) - idx_begin;
            if (arr_sz > safe_popsz) arr_sz = safe_popsz;

            fprintf(stderr, "p2[elem %hu] => %p\n", arr_sz, (void *)arr_out);
            safe_popsz -= (unsigned)ielb_ioux_xsq_pop_to(&pud->sqofq, arr_out, (size_t)arr_sz);
            fprintf(stderr, "p2E r=%hu\n", safe_popsz);

            arr_out = pud->mapptr_sqes;
            arr_sz = idx_begin < safe_popsz ? idx_begin : safe_popsz;
            fprintf(stderr, "p2[elem %hu] => %p\n", arr_sz, (void *)arr_out);
            safe_popsz -= ielb_ioux_xsq_pop_to(&pud->sqofq, arr_out, (size_t)arr_sz);
            fprintf(stderr, "p2E r=%hu\n", safe_popsz);
        }
        {
            printf("after: (struct iel_que_st) { .map=%p, .mapcap=%zu, .chunk_s=%zu, .os_s=%hu, .chunk_e=%zu, .os_e=%hu }\nmap: [", pud->sqofq.map, pud->sqofq.mapcap, pud->sqofq.chunk_s, pud->sqofq.os_s, pud->sqofq.chunk_e, pud->sqofq.os_e);
            for (iel_que_sz i = 0; i < pud->sqofq.mapcap; ++i) {
                printf("%p; ", (void *)(((void ***)pud->sqofq.map)[i]));
            }
            puts("]");
            printf("que: [");
            iel_que_idx it_ch = pud->sqofq.chunk_s;
            iel_que_offset it_os = pud->sqofq.os_s;
            while (it_ch != pud->sqofq.chunk_e || it_os != pud->sqofq.os_e) {
                struct io_uring_sqe *s = &((struct io_uring_sqe **)pud->sqofq.map)[it_ch][it_os];
                printf("{OP=%hu,UD=%p}; ", s->opcode, (void *)s->user_data);
                it_os++;
                it_os &= ((1ULL << SQOFQ_CHUNK_SZ_BIT) - 1);
                if (!it_os) ++it_ch;
            }
            puts("]");
        }
    }

    while (1) {
        /* Get the entry */
        struct io_uring_cqe const *cqe = &pud->cqes[pud->lcl_chead & pud->cring_mask];

        if (cqe->user_data) {
            fprintf(stderr, "C: %p\n", (void *)cqe->user_data);
            iel_cb cb = *(iel_cb *)iel_tp_untag((void *)cqe->user_data, IEL_CB_ALIGN).ptr;
            cb((void *)cqe->user_data, cqe->res);
        }

        ++pud->lcl_chead;

        /* CQ empty */
        if (pud->lcl_chead == ld_acq(pud->cring_tail)) break;

        ielb_ioux_drainque(&pud->taskque);
    }
    return 0;
}

static
volatile sig_atomic_t setup_trapped = 0;

int ielb_ioux_lnew_us(void *ctx, union iel_arg_un flags) {
    (void) flags;
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    struct io_uring_params p;
    int sring_sz, cring_sz;

    /* See io_uring_setup(2) for io_uring_params.flags you can set */
    memset(&p, 0, sizeof(p));
    setup_trapped = 0;
    pud->ring_fd = io_uring_setup(QUEUE_DEPTH, &p);
    if (setup_trapped) {
        // TODO: handle errors
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
    sring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    cring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);

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
    pud->sring_tail = PTR_OFFSET(pud->mapptr_sq, p.sq_off.tail, _Atomic(unsigned) *);
    pud->sring_head = PTR_OFFSET(pud->mapptr_sq, p.sq_off.head, _Atomic(unsigned) const *);
    pud->sring_mask = *PTR_OFFSET(pud->mapptr_sq, p.sq_off.ring_mask, unsigned const *);
    pud->sring_array = PTR_OFFSET(pud->mapptr_sq, p.sq_off.array, unsigned *);

    /* Map in the submission queue entries array */
    pud->mapptr_sqes = (struct io_uring_sqe *)mmap(0, pud->maplen_sqes,
                   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                   pud->ring_fd, IORING_OFF_SQES);
    if (pud->mapptr_sqes == MAP_FAILED) {
        perror("mmap");
        goto fail_unmapcq;
    }

    /* Save useful fields for later easy reference */
    pud->cring_head = PTR_OFFSET(pud->mapptr_cq, p.cq_off.head, _Atomic(unsigned) *);
    pud->cring_tail = PTR_OFFSET(pud->mapptr_cq, p.cq_off.tail, _Atomic(unsigned) const *);
    pud->cring_mask = *PTR_OFFSET(pud->mapptr_cq, p.cq_off.ring_mask, unsigned *);
    pud->cqes = PTR_OFFSET(pud->mapptr_cq, p.cq_off.cqes, struct io_uring_cqe *);

    if (iel_quep_init(&pud->taskque, 0) < 0)
        goto fail_unmapsqes;

    if (ielb_ioux_xsq_init(&pud->sqofq, 0) < 0)
        goto fail_deltaskq;

    pud->lcl_stail = *pud->sring_tail;
    pud->lcl_chead = *pud->cring_head;
    pud->feat = IEL_FEAT_AVAIL | IEL_FEAT_ETIME_MICROS;

    return 0;
fail_deltaskq:;
    iel_quep_del(&pud->taskque);
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
    struct ielb_ioux_lnewt_arg *arg = (struct ielb_ioux_lnewt_arg *)_arg;
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
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    ielb_ioux_xsq_del(&pud->sqofq);
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

    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    switch (op) {
        case IELB_IOU_XCNTL_SQLEN:
            return (union iel_arg_un) { .ull = (unsigned)(pud->lcl_stail - ld_acq(pud->sring_head)) };
        default:
            return (union iel_arg_un) { .ull = (unsigned long long) -1 };
    }
}

const unsigned long long ielb_iou_cap = IEL_FEAT_AVAIL | IEL_FEAT_ETIME_MICROS;

unsigned long long ielb_iou_xfeat(void *ctx, union iel_arg_un flags) {
    (void) flags;
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
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
#define SETUP_VTABLE_FN(x, y) vt->p_##x = &ielb_iou_##y
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
        vt->p_lnew = &ielb_ioux_lnew_us;
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
