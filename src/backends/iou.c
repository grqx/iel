#ifdef __linux__
// TODO: check: minimum 6.0 kernel
// TODO: detect __STDC_NO_ATOMICS__, add build system option for en/disabling the backend
// XXX: known limitations: IOSQE_IO_LINK maximum batch size = 8
// XXX: never use IOSQE_IO_HARDLINK without IOSQE_IO_LINK, or
// any flag that implies IOSQE_IO_LINK
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <linux/prctl.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include <linux/io_uring.h>

#include <iel/arg.h>
#include <iel/backends.h>
#include <iel/backends/iou.h>
#include <iel/tagptr.h>
#include <iel/errno.h>
#include <iel_priv/quep.h>
#include <iel_priv/bitmap.h>

#ifdef NDEBUG
#define DBG(...) ((void) 0)
#else
#define DBG(...) (fprintf(stderr, __VA_ARGS__))
#endif

#define SQOFQ_CHUNK_SZ_BIT 10
static_assert(SQOFQ_CHUNK_SZ_BIT > 3, "Chunk too small");
#define IEL_QUE_IMPL
#define IEL_QUE_TPL (/*ChunkEntsBit=*/SQOFQ_CHUNK_SZ_BIT, /*MinChunks=*/8, /*type=*/struct io_uring_sqe, /*pfx=*/ielb_ioux_xsq_, /*sfx=*/, /*api=*/static inline, /*align=*/64,)
#include <iel_priv/que.tpl.h>

// Non-SQPOLL saturates at 64, 128 is better for SQPOLL
#define QUEUE_DEPTH_BIT 6
static_assert(QUEUE_DEPTH_BIT > 3, "Queue too small");
#define QUEUE_DEPTH (1ULL << QUEUE_DEPTH_BIT)
#define PTR_OFFSET(voidp, os, typ) ((typ)((unsigned char *)voidp + os))

#define ld_rlx(atomic_p) atomic_load_explicit(atomic_p, memory_order_relaxed)
#define ld_acq(atomic_p) atomic_load_explicit(atomic_p, memory_order_acquire)
#define st_rlx(atomic_p, new_val) atomic_store_explicit(atomic_p, new_val, memory_order_relaxed)
#define st_rel(atomic_p, new_val) atomic_store_explicit(atomic_p, new_val, memory_order_release)

#define IOU_FEATS (IEL_FEAT_AVAIL | IEL_FEAT_ETIME_MICROS | IEL_FEAT_REQLNK | IEL_FEAT_NOREG_HANDLE)

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
int io_uring_register(unsigned int fd, unsigned int opcode, void *arg, unsigned int nr_args)
{
    int ret = syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
    return (ret < 0) ? -errno : ret;
}

static inline
int io_uring_enter(int ring_fd, unsigned int to_submit,
                   unsigned int min_complete, unsigned int flags, void *arg, size_t sz)
{
    do {
        int ret = syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                          flags, arg, sz);
        if IEL_LIKELY (ret >= 0)
            return ret;
        else if (errno == EINTR) {
            DBG("io_uring_enter: EINTR\n");
            continue;
        }
        else
            return -errno;
    } while (1);
}

static inline
iel_taskres ielb_ioux_regfiles(struct ielb_iou_ctx_st *pud, iel_pf_reg_fd const *fds, iel_pf_reg_fd *out, size_t nr_args) {
    static_assert(IEL_PF_FD_R_INVAL == IEL_PF_SOCKFD_R_INVAL, "sock/fd r inval should equal");
    iel_taskres err_ret = IOU_FEATS & IEL_FEAT_NOREG_HANDLE ? 0 : -1;

    if (!pud->filetable_len) {
        for (size_t i = 0; i < nr_args; ++i)
            out[i].reg = IEL_PF_FD_R_INVAL;
        return iel_errno = IEL_ENOBUFS, err_ret;
    }

    if IEL_UNLIKELY (!nr_args)
        return iel_errno = 0, 0;

    size_t i = 0, it = 0, bmsz = (pud->filetable_len + IEL_BM_BITS_IN_ELEM - 1) / IEL_BM_BITS_IN_ELEM;
    do {
        size_t idx = iel_bm_scan_clear((iel_bm_elem *)pud->filetable_bitmap, bmsz, &it);

        if (idx == IEL_BM_NPOS) {
            --i;
            iel_errno = IEL_ENOBUFS;
            goto nobufs;
        }

        pud->filetable[idx] = fds[i].raw;
        out[i].raw = fds[i].raw;
        out[i].reg = idx;
        ++i;
    } while (i < nr_args && it < bmsz);

    {
        struct io_uring_rsrc_update upd = {
            .offset=0,
            .data=(uint64_t)pud->filetable,
        };
        /* TODO: dereg + close taking regfd as xreg opcode f/s? */
        if (io_uring_register(pud->ring_fd, IORING_REGISTER_FILES_UPDATE, &upd, pud->filetable_len) < 0) {
            perror("iou/xreg reg: IORING_REGISTER_FILES_UPDATE");
            iel_errno = IEL_E_NOIMPL;
            goto nobufs;
        }
    }

    return nr_args;

nobufs:;
    while (i != SIZE_MAX) {
        iel_bm_set((iel_bm_elem *)pud->filetable_bitmap, bmsz, out[i].reg);
        pud->filetable[out[i].reg] = -1;
        out[i].reg = IEL_PF_FD_R_INVAL;
        --i;
    }
    return err_ret;
}

iel_taskres ielb_iou_xreg(void *ctx, unsigned char opcode, void const * IEL_CQUAL_RESTRICT in, void * IEL_CQUAL_RESTRICT out, size_t nr_args, union iel_arg_un flags) {
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    iel_errno = IEL_E_NOIMPL;
    switch (opcode) {
        case IEL_XREG_FILES:;
        case IEL_XREG_SOCKETS:;
            if (flags.ull)
                return iel_errno = IEL_EINVAL, -1;
            return ielb_ioux_regfiles(pud, (iel_pf_reg_fd const *)(in ? in : out), (iel_pf_reg_fd *)out, nr_args);
        case IEL_XREG_DE_FILES:;
        case IEL_XREG_DE_SOCKETS:;
        {
            if (nr_args != 1)
                return iel_errno = IEL_EINVAL, -1;
            iel_pf_reg_fd const *fd = (iel_pf_reg_fd const *)in;

            if (flags.ull & IEL_FLAG_XREG_DRG) {
                iel_pf_fd_r fd_r = fd->reg;
                pud->filetable[fd_r] = -1;
                struct io_uring_rsrc_update upd = {
                    .offset=0,
                    .data=(uint64_t)pud->filetable,
                };
                if (io_uring_register(
                        pud->ring_fd, IORING_REGISTER_FILES_UPDATE, &upd,
                        pud->filetable_len) < 0) {
                    perror("iou/xreg dereg: IORING_REGISTER_FILES_UPDATE");
                    return iel_errno = IEL_E_NOIMPL, -1;
                }
                iel_bm_set(
                    (iel_bm_elem *)pud->filetable_bitmap,
                    (pud->filetable_len + IEL_BM_BITS_IN_ELEM - 1) / IEL_BM_BITS_IN_ELEM,
                    fd_r);
            }
            if (flags.ull & IEL_FLAG_XREG_DEL) {
                int res = close(fd->raw);
                if (res < 0) {
                    perror("iou/xreg close: IORING_REGISTER_FILES_UPDATE");
                    return iel_errno = IEL_E_NOIMPL, res;
                }
            }
            return 1;
        }
        default:;
            break;
    }
    return iel_errno = IEL_EINVAL, -1;
}

/*
 * Submit a request to the submission queue.
 * */
static inline
void *submit_to_sq(struct ielb_iou_ctx_st *pud, unsigned char op, int fd, long long off, unsigned long long addr, unsigned int len, void *user_data, unsigned long long flags) {
    // TODO: handle CQ overflow/-EBUSY? see IORING_FEAT_NODROP in io_uring_setup(2)
    unsigned tail = pud->lcl_stail;
    unsigned sq_ents = (unsigned)(tail - ld_acq(pud->sring_head));
    struct io_uring_sqe *sqe;

    if IEL_UNLIKELY (flags & ~(IEL_FLAG_NOREG_HANDLE | IEL_FLAG_REQLNK))
        return iel_errno = IEL_EINVAL, NULL;

    if (sq_ents == pud->sring_mask + 1) {  // SQ full
        sqe = ielb_ioux_xsq_rsv1(&pud->sqofq);
        if (!sqe)
            return iel_errno = IEL_ENOMEM, NULL;
        DBG("sq full!\n");
    } else {
        unsigned index;
        DBG("pushing event into sq, current size: %u\n", sq_ents);
        index = tail & pud->sring_mask;
        /* Add our submission queue entry to the tail of the SQE ring buffer */
        sqe = &pud->mapptr_sqes[index];
        pud->sring_array[index] = index;
        /* Update the tail */
        ++pud->lcl_stail;
    }

    /* Fill in the parameters required for the operation */
    sqe->opcode = op;

    sqe->flags = IOSQE_FIXED_FILE;
    if IEL_UNLIKELY (flags & IEL_FLAG_REQLNK)
        sqe->flags |= IOSQE_IO_LINK | IOSQE_CQE_SKIP_SUCCESS;
    if (flags & IEL_FLAG_NOREG_HANDLE)
        sqe->flags &= ~IOSQE_FIXED_FILE;

    sqe->ioprio = 0;
    sqe->fd = fd;
    sqe->off = (unsigned long long)off;
    sqe->addr = addr;
    sqe->len = len;
    sqe->rw_flags = 0;
    sqe->user_data = (unsigned long long)user_data;
    // other unused fields
    sqe->buf_index = sqe->personality = sqe->file_index = sqe->optval = 0;

    DBG("submit opc %d, flags %d, fd: %d\n", sqe->opcode, sqe->flags, sqe->fd);

    // TODO: flush immediately if SQPOLL(?)

    return user_data;
}


void *ielb_iou_fpr(void *ctx, iel_pf_fd_r fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_READ, fd, offset, (unsigned long long)buf, count, cbp, flags.ull);
}
void *ielb_iou_fprv(void *ctx, iel_pf_fd_r fd, iel_pf_iov *iovecs, size_t iovcnt, iel_pf_pos offset, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_READV, fd, offset, (unsigned long long)iovecs, iovcnt, cbp, flags.ull);
}
void *ielb_iou_fpw(void *ctx, iel_pf_fd_r fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_WRITE, fd, offset, (unsigned long long)buf, count, cbp, flags.ull);
}
void *ielb_iou_fpwv(void *ctx, iel_pf_fd_r fd, iel_pf_iov *iovecs, size_t iovcnt, iel_pf_pos offset, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_WRITEV, fd, offset, (unsigned long long)iovecs, iovcnt, cbp, flags.ull);
}

void *ielb_ioux_r(void *ctx, iel_pf_fd_r fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_READ, fd, (unsigned long long)-1, (unsigned long long)buf, count, cbp, flags.ull);
}
void *ielb_ioux_rv(void *ctx, iel_pf_fd_r fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_READV, fd, (unsigned long long)-1, (unsigned long long)iov, iovcnt, cbp, flags.ull);
}
void *ielb_ioux_w(void *ctx, iel_pf_fd_r fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_WRITE, fd, (unsigned long long)-1, (unsigned long long)buf, count, cbp, flags.ull);
}
void *ielb_ioux_wv(void *ctx, iel_pf_fd_r fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_WRITEV, fd, (unsigned long long)-1, (unsigned long long)iov, iovcnt, cbp, flags.ull);
}

void *ielb_iou_sa(void *ctx, iel_pf_sockfd_r fd, iel_pf_sockaf *addr_out, iel_pf_socklen *addrlen_out, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_ACCEPT, fd, (unsigned long long)addrlen_out, (unsigned long long)addr_out, 0, cbp, flags.ull);
}
void *ielb_iou_sc(void *ctx, iel_pf_sockfd_r fd, iel_pf_sockaf *addr, iel_pf_socklen addrlen, union iel_arg_un flags, void *cbp) {
    return submit_to_sq((struct ielb_iou_ctx_st *)ctx, IORING_OP_CONNECT, fd, (unsigned long long)addrlen, (unsigned long long)addr, 0, cbp, flags.ull);
}

struct ielb_ioux_etime_cbt {
    IEL_CB_BASE base;
    struct timespec ts;
    void *user_data;
};

static
void ielb_ioux_etime_cb(void *_self, iel_taskres res) {
    iel_cb user_cb;
    void *user_data;
    {
        struct ielb_ioux_etime_cbt *cbp = (struct ielb_ioux_etime_cbt *)_self;
        user_data = cbp->user_data;

        user_cb = ((struct iel_cb_base *)iel_tp_untag(user_data, IEL_CB_ALIGN).ptr)->cb;

        free(cbp);
    }
    user_cb(user_data, res == -ETIME ? 0 : res);
}

void *ielb_iou_etime(void *ctx, unsigned long long time, union iel_arg_un flags, void *user_data) {
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    struct ielb_ioux_etime_cbt *wcbp = (struct ielb_ioux_etime_cbt *)malloc(sizeof(struct ielb_ioux_etime_cbt));
    if (!wcbp)
        return iel_errno = IEL_ENOMEM, NULL;
    if (flags.ull & IEL_FLAG_ETIME_MICROS) {
        wcbp->ts.tv_sec = time / 1000000;
        wcbp->ts.tv_nsec = (time % 1000000) * 1000;
    } else {
        wcbp->ts.tv_sec = time / 1000;
        wcbp->ts.tv_nsec = (time % 1000) * 1000000;
    }
    if (flags.ull & ~IEL_FLAG_ETIME_MICROS) {
        free(wcbp);
        return iel_errno = IEL_EINVAL, NULL;
    }
    wcbp->base.cb = &ielb_ioux_etime_cb;
    wcbp->user_data = user_data;

    return submit_to_sq(pud, IORING_OP_TIMEOUT, 0, 0, (unsigned long long)&wcbp->ts, 1, &wcbp->base, IEL_FLAG_NOREG_HANDLE);
}

void *ielb_iou_esoon(void *ctx, union iel_arg_un flags, void *user_data) {
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    if IEL_UNLIKELY (flags.ull & ~IEL_FLAG_ESOON_NEXT)
        return iel_errno = IEL_EINVAL, NULL;
    void **pout = iel_quep_rsv1(flags.ull & IEL_FLAG_ESOON_NEXT ? &pud->prepque : &pud->soonque);
    if IEL_UNLIKELY (!pout)
        return iel_errno = IEL_ENOMEM, NULL;
    return *pout = user_data;
}

static inline
void ielb_ioux_exeq(struct iel_que_st *que) {
    iel_que_idx chunk_stop = que->chunk_e;
    iel_que_offset os_stop = que->os_e;
    while (1) {
        void *task;
        int res = iel_quep_pop1(que, &task, chunk_stop, os_stop);
        if IEL_UNLIKELY (res < 0) break;  // empty

        iel_cb user_cb = ((struct iel_cb_base *)iel_tp_untag(task, IEL_CB_ALIGN).ptr)->cb;
        user_cb(task, 0);

        if IEL_UNLIKELY (res > 0) break;  // last
    };
}

int ielb_iou_lrun1(void *ctx, union iel_arg_un flags) {
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    size_t xsq_sz;
    unsigned sq_len;
    DBG("enter lrun1\n");
    if IEL_UNLIKELY (flags.ull)
        return iel_errno = IEL_EINVAL, -1;
    ielb_ioux_exeq(&pud->prepque);
    ielb_ioux_exeq(&pud->soonque);

    // relaxed as no synchronisation required
    sq_len = pud->lcl_stail - ld_rlx(pud->sring_head);
    DBG("awaiting submission of %u SQEs and completion of 1 event; submitted: [\n", sq_len);
    for (unsigned idx = ld_acq(pud->sring_head); idx < pud->lcl_stail; ++idx) {
        unsigned real_idx = pud->sring_array[idx & pud->sring_mask];
        struct io_uring_sqe *s = &pud->mapptr_sqes[real_idx];
        DBG("  {OP=%hu,\tUD=%p}@%p\n", s->opcode, (void *)s->user_data, (void *)s);
        (void) s;
    }
    DBG("]\n");
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

    struct timespec ts = {
        .tv_sec=0,
        .tv_nsec=0,
    };
    struct io_uring_getevents_arg gearg = {
        .sigmask=(uint64_t)NULL,
        .sigmask_sz=0,
        .ts=(uint64_t)NULL,
    };
    if (iel_quep_size(&pud->soonque) || iel_quep_size(&pud->prepque)) {
        DBG("zeroing timeout\n");
        gearg.ts = (uint64_t) &ts;
    }
    int ret = io_uring_enter(pud->ring_fd_registered, sq_len, 1,
                             IORING_ENTER_GETEVENTS | IORING_ENTER_REGISTERED_RING | IORING_ENTER_EXT_ARG,
                             &gearg, sizeof(struct io_uring_getevents_arg));
    if (gearg.ts == (uint64_t)&ts) goto out;
    if IEL_UNLIKELY (ret < 0)
        return iel_errno = -ret, -1;

    xsq_sz = ielb_ioux_xsq_size(&pud->sqofq);
    DBG("XSQ has %zu; reading from cq %u entries\n", xsq_sz, ld_acq(pud->cring_tail) - pud->lcl_chead);
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
                if IEL_UNLIKELY (!(flags & IOSQE_IO_LINK))
                    break;

                if IEL_UNLIKELY (!os_tail--)
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
            // TODO: pop to sqes[0], and fill indirection array accordingly?
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

            DBG("p2[elem %hu] => %p\n", arr_sz, (void *)arr_out);
            safe_popsz -= (unsigned)ielb_ioux_xsq_pop_to(&pud->sqofq, arr_out, (size_t)arr_sz);
            DBG("p2E r=%hu\n", safe_popsz);

            arr_out = pud->mapptr_sqes;
            arr_sz = idx_begin < safe_popsz ? idx_begin : safe_popsz;
            DBG("p2[elem %hu] => %p\n", arr_sz, (void *)arr_out);
            safe_popsz -= ielb_ioux_xsq_pop_to(&pud->sqofq, arr_out, (size_t)arr_sz);
            DBG("p2E r=%hu\n", safe_popsz);
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
        /* Assuming CQ non-empty, get the entry */
        struct io_uring_cqe const *cqe = &pud->cqes[pud->lcl_chead & pud->cring_mask];

        void *user_data = (void *)cqe->user_data;
        DBG("[iou@%p] C: %p; %td\n", (void *)pud, user_data, (ptrdiff_t)cqe->res);
        if (user_data) {
            iel_cb user_cb = ((struct iel_cb_base *)iel_tp_untag(user_data, IEL_CB_ALIGN).ptr)->cb;
            user_cb(user_data, cqe->res);
        }

        ++pud->lcl_chead;

        /* CQ empty */
        if (pud->lcl_chead == ld_acq(pud->cring_tail)) break;

        ielb_ioux_exeq(&pud->soonque);
    }

out:;
    DBG("lrun1 normal exit\n");
    return 0;
}

struct ielb_ioux_thsetup_cbt {
    int res;
    const unsigned entries;
    struct io_uring_params * const p;
};

static
void ielb_ioux_nop(int signo) { (void)signo; pthread_exit(NULL); }

static
void *ielb_ioux_thsetup_cb(void *_arg) {
    struct ielb_ioux_thsetup_cbt *arg = (struct ielb_ioux_thsetup_cbt *)_arg;
    struct sigaction oact, act = {
        .sa_handler=&ielb_ioux_nop,
    };
    if (sigemptyset(&act.sa_mask) < 0)
        return NULL;
    if (sigaction(SIGSYS, &act, &oact) < 0)
        return NULL;
    arg->res = io_uring_setup(arg->entries, arg->p);
    if (sigaction(SIGSYS, &oact, NULL) < 0)
        return NULL;
    return _arg;
}

static inline
int ielb_ioux_lnew_base(struct ielb_iou_ctx_st *pud, long max_files, long max_bufs, union iel_arg_un flags, unsigned char do_thread) {
    // TODO: max_bufs
    (void) max_bufs;

    struct io_uring_params p;
    int sring_sz, cring_sz;

    if IEL_UNLIKELY (flags.ull)
        return -1;

    /* See io_uring_setup(2) for io_uring_params.flags you can set */
    memset(&p, 0, sizeof(p));
    if (do_thread) {
        struct ielb_ioux_thsetup_cbt arg = { .entries=QUEUE_DEPTH, .p=&p };
        pthread_t pth;
        int res;
        void *tres = NULL;
        res = pthread_create(&pth, NULL, ielb_ioux_thsetup_cb, &arg);
        if (res) {
            DBG("pthread_create: %s\n", strerror(res));
            return -1;
        }
        res = pthread_join(pth, &tres);
        if (res) {
            DBG("pthread_join: %s\n", strerror(res));
            return -1;
        }
        if (tres != &arg) {
            DBG("setup thread returned failure\n");
            return -1;
        }
        pud->ring_fd = arg.res;
    } else
        pud->ring_fd = io_uring_setup(QUEUE_DEPTH, &p);

    if (pud->ring_fd < 0) {
        perror("io_uring_setup");
        goto fail;
    }
    DBG("ring fd: %d\n", pud->ring_fd);

    {
        unsigned required_feats = IORING_FEAT_NODROP | IORING_FEAT_RW_CUR_POS | IORING_FEAT_SUBMIT_STABLE;
        if ((p.features & required_feats) != required_feats)
            goto fail_closefd;
    }

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

    if (iel_quep_init(&pud->soonque, 0) < 0)
        goto fail_unmapsqes;

    if (iel_quep_init(&pud->prepque, 0) < 0)
        goto fail_delsoonque;

    if (ielb_ioux_xsq_init(&pud->sqofq, 0) < 0)
        goto fail_delprepque;

    pud->lcl_stail = *pud->sring_tail;
    pud->lcl_chead = *pud->cring_head;

    {
        static const unsigned NEXT_SLOT = (unsigned)-1;
        struct io_uring_rsrc_update reg_arg = {
            .data=pud->ring_fd,
            .offset=NEXT_SLOT,
        };
        if (io_uring_register(pud->ring_fd, IORING_REGISTER_RING_FDS, &reg_arg, 1) < 0) {
            perror("IORING_REGISTER_RING_FDS");
            goto fail_delxsq;
        }
        pud->ring_fd_registered = reg_arg.offset;
        if (pud->ring_fd_registered == NEXT_SLOT) {
            DBG("IORING_REGISTER_RING_FDS didn't change arg.offset");
            goto fail_delxsq;
        }
        DBG("offs:%d\n", pud->ring_fd_registered);
    }

    {
        struct rlimit rlres;
        if (getrlimit(RLIMIT_NOFILE, &rlres) < 0) {
            perror("getrlimit");
            goto fail_unregring;
        }
        unsigned long max_allowed = rlres.rlim_cur < (1UL << 20) ? rlres.rlim_cur : (1UL << 20);
        if (max_files == -1)
            max_files = max_allowed;
        else if (max_files > 0)
            max_files = max_allowed < max_files ? max_allowed : max_files;
        else if (max_files == 0) {
            pud->filetable_len = 0;
            goto no_prealloc_file;
        }
        else
            goto fail_unregring;

        pud->filetable_len = max_files;
        pud->filetable = (iel_pf_fd *)malloc(pud->filetable_len * sizeof(iel_pf_fd));
        if (!pud->filetable)
            goto fail_unregring;

        size_t bm_len = (pud->filetable_len + IEL_BM_BITS_IN_ELEM - 1) / IEL_BM_BITS_IN_ELEM;
        pud->filetable_bitmap = malloc(bm_len * sizeof(iel_bm_elem));
        if (!pud->filetable_bitmap)
            goto fail_freeft;

        for (size_t i = 0; i < pud->filetable_len; ++i)
            pud->filetable[i] = -1;
        for (size_t i = 0; i < bm_len; ++i)
            ((iel_bm_elem *)pud->filetable_bitmap)[i] = IEL_BM_ELEM_MAX;

        if (io_uring_register(pud->ring_fd, IORING_REGISTER_FILES, pud->filetable, max_files) < 0) {
            perror("IORING_REGISTER_FILES");
            goto fail_freeftbm;
        }
        fprintf(stderr, "managed to register %zu file slots\n", (size_t)max_files);
    }
no_prealloc_file:;

    pud->feat = IOU_FEATS;

    return 0;
//fail_unregfile:;
//    if (pud->filetable_len && io_uring_register(pud->ring_fd, IORING_UNREGISTER_FILES, NULL, 0) < 0)
//        perror("IORING_UNREGISTER_FILES");
fail_freeftbm:;
    free(pud->filetable_bitmap);
fail_freeft:;
    free(pud->filetable);
fail_unregring:;
    if (io_uring_register(pud->ring_fd, IORING_UNREGISTER_RING_FDS,
            &(struct io_uring_rsrc_update) { .offset=pud->ring_fd_registered }, 1) < 0)
        perror("IORING_UNREGISTER_RING_FDS");
fail_delxsq:;
    ielb_ioux_xsq_del(&pud->sqofq);
fail_delprepque:;
    iel_quep_del(&pud->prepque);
fail_delsoonque:;
    iel_quep_del(&pud->soonque);
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
    return -1;
}

int ielb_iou_lnew(void *ctx, long max_files, long max_bufs, union iel_arg_un flags) {
    return ielb_ioux_lnew_base((struct ielb_iou_ctx_st *)ctx, max_files, max_bufs, flags, 1);
}

int ielb_ioux_lnew_us(void *ctx, long max_files, long max_bufs, union iel_arg_un flags) {
    return ielb_ioux_lnew_base((struct ielb_iou_ctx_st *)ctx, max_files, max_bufs, flags, 0);
}

void ielb_iou_ldel(void *ctx) {
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    if (pud->filetable_len && io_uring_register(pud->ring_fd, IORING_UNREGISTER_FILES, NULL, 0) < 0)
        perror("IORING_UNREGISTER_FILES");
    free(pud->filetable_bitmap);
    free(pud->filetable);
    if (io_uring_register(pud->ring_fd, IORING_UNREGISTER_RING_FDS,
            &(struct io_uring_rsrc_update) { .offset=pud->ring_fd_registered }, 1) < 0)
        perror("IORING_UNREGISTER_RING_FDS");

    ielb_ioux_xsq_del(&pud->sqofq);
    iel_quep_del(&pud->prepque);
    iel_quep_del(&pud->soonque);

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

const unsigned long long ielb_iou_cap = IOU_FEATS;

unsigned long long ielb_iou_xfeat(void *ctx, union iel_arg_un flags) {
    struct ielb_iou_ctx_st *pud = (struct ielb_iou_ctx_st *)ctx;
    if IEL_UNLIKELY (!ctx || flags.ull) return 0;
    return pud->feat;
}

void (ielb_ioux_nop_a)(union iel_arg_un flags) {
    (void) flags;
}

unsigned char ielb_iou_vtsetup(struct iel_vtable_st *vt) {
    if (!vt)
        return iel_errno = IEL_EINVAL, IEL_VTSETUP_RET_ERROR;
#define IEL_BACKEND_FNS_ITER(name) vt->p_ ## name = &ielb_iou_ ## name;
    IEL_BACKEND_FNS

    int scmode = prctl(PR_GET_SECCOMP);
    // scmode won't be SECCOMP_MODE_STRICT since prctl would be blocked in that case
    if (scmode == SECCOMP_MODE_DISABLED) {
        vt->p_lnew = &ielb_ioux_lnew_us;
    } else {
        if (scmode == SECCOMP_MODE_FILTER) DBG("SECCOMP_MODE_FILTER\n");
        // Android (app) uses SECCOMP_RET_TRAP (syscall ret 64), and docker uses SECCOMP_RET_ERRNO | EPERM (or lsm?)
        // doesn't work for SECCOMP_RET_KILL_PROCESS
        // Note: the use of SECCOMP_RET_KILL_THREAD to kill a single thread in a multithreaded process is likely to leave the process in a permanently inconsistent and possibly corrupt state.

    }
#undef IEL_BACKEND_FNS_ITER

    return IEL_VTSETUP_RET_UNSURE;
}

#else  /* ifdef __linux__ */
#include <iel/arg.h>
#include <iel/backends.h>
#include <iel/backends/iou.h>
unsigned char ielb_iou_vtsetup(struct iel_vtable_st *vt) {
    (void) vt;
    return IEL_VTSETUP_RET_UNAVAIL;
}
#endif  /* ifdef __linux__ */
