#ifndef IEL_BACKENDS_IOU_H_
#define IEL_BACKENDS_IOU_H_

#include <iel/backends.h>

unsigned char ielb_iou_vtsetup(struct iel_vtable_st *vt);

struct iel_iou_ctx_st;

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

/* maybe this should return a handle that allows cancelling */
void ielb_iou_etime(void *ctx, unsigned long long time, union iel_arg_un flags, iel_cbp cbp);
void ielb_iou_esoon(void *ctx, union iel_arg_un flags, iel_cbp cbp);

int ielb_iou_lrun1(void *ctx, union iel_arg_un flags);
size_t ielb_iou_lsize(void);
/* unsafe variant, could crash the thread but might be slightly faster */
int ielb_ioux_lnew_us(void *ctx, union iel_arg_un flags);
int ielb_iou_lnew(void *ctx, union iel_arg_un flags);
void ielb_iou_ldel(void *ctx);

union iel_arg_un ielb_iou_xcntl(void *ctx, unsigned short op, union iel_arg_un arg0, union iel_arg_un arg1);
unsigned long long ielb_iou_xfeat(void *ctx, union iel_arg_un flags);
void ielb_ioux_nop_a(union iel_arg_un flags);

#define ielb_iou_xinit ielb_ioux_nop_a
#define ielb_iou_xtdwn ielb_ioux_nop_a
#define ielb_ioux_nop_a(_)

extern unsigned long long ielb_iou_cap;

/* Query Submission Queue Length
 * receives: arg0 = (unused), arg1 = (unused)
 * returns: .ull = (unsigned) length of the Submission Queue
 */
#define IELB_IOU_XCNTL_SQLEN 0

#endif /* ifndef IEL_BACKENDS_IOU_H_ */
