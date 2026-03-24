#ifndef IEL_BACKENDS_IOU_H_
#define IEL_BACKENDS_IOU_H_

#include <stddef.h>

#include <iel/config.h>
#include <iel/backends.h>
#include <iel/platform.h>
#include <iel/arg.h>
#include <iel/quedecl.h>

IEL_STABLE_API
iel_fn_vtsetup ielb_iou_vtsetup;

struct ielb_iou_ctx_st;

#define IEL_BACKEND_FNS_ITER(name) \
    IEL_API iel_fn_##name ielb_iou_##name;
IEL_BACKEND_FP_FNS
IEL_BACKEND_FNS_ITER(sa)
IEL_BACKEND_FNS_ITER(sc)
IEL_BACKEND_E_FNS
IEL_BACKEND_L_FNS
IEL_BACKEND_X_FNS
IEL_BACKEND_NEW_FNS
#undef IEL_BACKEND_FNS_ITER

IEL_API
void *ielb_ioux_r(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
IEL_API
void *ielb_ioux_rv(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);
IEL_API
void *ielb_ioux_w(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
IEL_API
void *ielb_ioux_wv(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);

#define ielb_iou_fr ielb_ioux_r
#define ielb_iou_frv ielb_ioux_rv
#define ielb_iou_fw ielb_ioux_w
#define ielb_iou_fwv ielb_ioux_wv
#define ielb_iou_sr ielb_ioux_r
#define ielb_iou_srv ielb_ioux_rv
#define ielb_iou_sw ielb_ioux_w
#define ielb_iou_swv ielb_ioux_wv

/* maybe this should return a handle that allows cancelling */
#ifndef IEL_USE_STABLE
struct io_uring_sqe;
struct io_uring_cqe;
// read mostly
struct ielb_iou_ctx_st {
    /* Submission ring pointers */
    _Atomic(unsigned) const *sring_head;  /* load-acquire */
    unsigned *sring_array;  /* store@os */
    struct io_uring_sqe *mapptr_sqes;  /* store@os */
    /* Completion ring pointers */
    _Atomic(unsigned) const *cring_tail;  /* load-acquire */
    struct io_uring_cqe const *cqes;  /* load@os */

    unsigned lcl_stail;
    unsigned sring_mask;

    _Atomic(unsigned) *sring_tail;  /* cache; store-release */
    _Atomic(unsigned) *cring_head;  /* cache; store-release */
    /* ----- 64B ----- */
    unsigned lcl_chead;
    unsigned cring_mask;

    struct iel_que_st taskque; /* TODO: confirm soon semantics */
    struct iel_que_st sqofq;  /* Submission Queue overflow queue */

    /* ----- cold(SQPOLL) ----- */
    unsigned ring_fd_registered;
    /* ----- cold(NORM) ----- */
    int ring_fd;
    unsigned int maplen_sqes;
    unsigned int maplen_sq;
    unsigned int maplen_cq;
    unsigned long long feat;
    void *mapptr_sq;
    void *mapptr_cq;
};
#define ielb_iou_lsize() sizeof(struct ielb_iou_ctx_st)
#endif

/* unsafe variant, could crash the thread but might be slightly faster */
IEL_API
int ielb_ioux_lnew_us(void *ctx, union iel_arg_un flags);

IEL_API
void ielb_ioux_nop_a(union iel_arg_un flags);

#define ielb_iou_xinit ielb_ioux_nop_a
#define ielb_iou_xtdwn ielb_ioux_nop_a
#define ielb_ioux_nop_a(_) ((void)0)

IEL_STABLE_GBL
const unsigned long long ielb_iou_cap;

/* Query Submission Queue Length
 * receives: arg0 = (unused), arg1 = (unused)
 * returns: .ull = (unsigned) length of the Submission Queue
 */
#define IELB_IOU_XCNTL_SQLEN 0

#endif /* ifndef IEL_BACKENDS_IOU_H_ */
