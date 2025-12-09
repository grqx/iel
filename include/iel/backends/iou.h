#ifndef IEL_BACKENDS_IOU_H_
#define IEL_BACKENDS_IOU_H_

#include <iel/backends.h>

unsigned char ielb_iou_vtsetup(struct iel_vtable_st *vt);

// TODO: make definition private
struct iel_iou_ctx_st {
    _Alignas(64)
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
    // ----- cold(SQPOLL) -----
    int ring_fd;
    // ----- cold(NORM) -----
    unsigned int maplen_sqes;
    unsigned int maplen_sq;
    unsigned int maplen_cq;
    void *mapptr_sq;
    void *mapptr_cq;
};

void ielb_iou_fpr(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp);
void ielb_iou_fprv(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovlen, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp);
void ielb_iou_fpw(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp);
void ielb_iou_fpwv(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovlen, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp);

void ielb_ioux_r(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, iel_cbp cbp);
void ielb_ioux_rv(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, iel_cbp cbp);
void ielb_ioux_w(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, iel_cbp cbp);
void ielb_ioux_wv(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, iel_cbp cbp);

#define ielb_iou_fr ielb_ioux_r
#define ielb_iou_frv ielb_ioux_rv
#define ielb_iou_fw ielb_ioux_w
#define ielb_iou_fwv ielb_ioux_wv
#define ielb_iou_sr ielb_ioux_r
#define ielb_iou_srv ielb_ioux_rv
#define ielb_iou_sw ielb_ioux_w
#define ielb_iou_swv ielb_ioux_wv

int ielb_iou_lrun1(void *ctx, union iel_arg_un flags);
size_t ielb_iou_lsize(void);
int ielb_iou_lnew(void *ctx, union iel_arg_un flags);
int ielb_iou_lnewt(void *ctx, union iel_arg_un flags);
void ielb_iou_ldel(void *ctx);

#endif /* ifndef IEL_BACKENDS_IOU_H_ */
