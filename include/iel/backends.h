#ifndef IEL_BACKENDS_H_
#define IEL_BACKENDS_H_

#include <stdalign.h>
#include <stddef.h>

#include <iel/platform.h>
#include <iel/arg.h>
#include <iel/tagptr.h>

typedef int iel_taskres;
typedef void (*iel_cb)(void *, iel_taskres);
/* TODO: add a flag for max-aligned callbacks(incompatible ABI!) */
#if 0
#define IEL_CB_ALIGN alignof(max_align_t)
#else
#define IEL_CB_ALIGN alignof(iel_cb)
#endif
#define IEL_CB_BASE alignas(IEL_CB_ALIGN) iel_cb

struct iel_cb_raw_st {
    IEL_CB_BASE base;
};

#ifdef IEL_USE_STABLE /* we don't need config.h for that */
#define IEL_RESOLVE_CALL_(be_vt,backend,func) ((be_vt).p_ ## func)
#else
#define IEL_RESOLVE_CALL_(be_vt,backend,func) ielb_ ## backend ## _ ## func
#endif

/* be_vt must be a valid struct iel_vtable_st
 * Use as: IEL_RESOLVE_CALL(iou_vt,iou,fpr,(...))
 */
#define IEL_RESOLVE_CALL(be_vt,backend,func,paren_args) (IEL_RESOLVE_CALL_(be_vt,backend,func) paren_args)

/* Adds a tag of 0 */
#define IEL_TAGCB(cb) (iel_tp_tag((void *)(cb), IEL_CB_ALIGN, 0))

// The backend will be available, p_lnew is expected to succeed
#define IEL_VTSETUP_RET_AVAIL ((unsigned char) '\x00')
// The backend will be unavailable, p_lnew must always fail
#define IEL_VTSETUP_RET_UNAVAIL ((unsigned char) '\x01')
// The backend's availability is unknown, p_lnew may or may not succeed
#define IEL_VTSETUP_RET_UNSURE ((unsigned char) '\x02')
// The user made a logic error when calling the vtsetup function
#define IEL_VTSETUP_RET_ERROR ((unsigned char) '\xff')

// TODO: REG_BUF, REG_FD, async accept

typedef void *(iel_fn_fpr)(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_fprv)(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovcnt, iel_pf_pos offset, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_fpw)(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_fpwv)(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovcnt, iel_pf_pos offset, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_fr)(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_frv)(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_fw)(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_fwv)(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);

typedef void *(iel_fn_sr)(void *ctx, iel_pf_sockfd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_srv)(void *ctx, iel_pf_sockfd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_sw)(void *ctx, iel_pf_sockfd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_swv)(void *ctx, iel_pf_sockfd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);

typedef void *(iel_fn_etime)(void *ctx, unsigned long long time, union iel_arg_un flags, void *cbp);
typedef void *(iel_fn_esoon)(void *ctx, union iel_arg_un flags, void *cbp);

typedef size_t (iel_fn_lsize)(void);
typedef int (iel_fn_lnew)(void *ctx, union iel_arg_un flags);
typedef void (iel_fn_ldel)(void *ctx);
typedef int (iel_fn_lrun1)(void *ctx, union iel_arg_un flags);

/* xfeat() works in two modes:
 * 1. static feature flags:
 *   When xfeat(NULL, IEL_ARG_NULL) & IEL_FEAT_AVAIL.
 *   The above call must happen after xinit(), and possibly before lnew().
 *   The return value of xfeat() must not change when called again,
 *   as long as the flags argument is valid.
 * 2. per-instance feature flags:
 *   When xfeat(ctx, IEL_ARG_NULL) & IEL_FEAT_AVAIL,
 *   the above call must happen after lnew(),
 *   passing the new loop instance as the ctx argument.
 *   The return value of xfeat() must not change for the same instance,
 *   as long as instance and the flags argument are valid.
 */
typedef unsigned long long (iel_fn_xfeat)(void *ctx, union iel_arg_un flags);
typedef union iel_arg_un (iel_fn_xcntl)(void *ctx, unsigned short op, union iel_arg_un arg0, union iel_arg_un arg1);
typedef void (iel_fn_xinit)(union iel_arg_un flags);
typedef void (iel_fn_xtdwn)(union iel_arg_un flags);

#define IEL_BACKEND_FP_FNS \
    /* fp/FilePosition series */ \
    IEL_BACKEND_FNS_ITER(fpr) \
    IEL_BACKEND_FNS_ITER(fprv) \
    IEL_BACKEND_FNS_ITER(fpw) \
    IEL_BACKEND_FNS_ITER(fpwv)

#define IEL_BACKEND_F_FNS \
    /* f/File series */ \
    IEL_BACKEND_FNS_ITER(fr) \
    IEL_BACKEND_FNS_ITER(frv) \
    IEL_BACKEND_FNS_ITER(fw) \
    IEL_BACKEND_FNS_ITER(fwv)

#define IEL_BACKEND_S_FNS \
    /* s/Socket series */ \
    IEL_BACKEND_FNS_ITER(sr) \
    IEL_BACKEND_FNS_ITER(srv) \
    IEL_BACKEND_FNS_ITER(sw) \
    IEL_BACKEND_FNS_ITER(swv)

#define IEL_BACKEND_E_FNS \
    /* e/Execute series */ \
    IEL_BACKEND_FNS_ITER(etime) \
    IEL_BACKEND_FNS_ITER(esoon)

#define IEL_BACKEND_L_FNS \
    /* l/Loop series */ \
    IEL_BACKEND_FNS_ITER(lnew) \
    IEL_BACKEND_FNS_ITER(ldel) \
    IEL_BACKEND_FNS_ITER(lrun1) \
    IEL_BACKEND_FNS_ITER(lsize)

#define IEL_BACKEND_X_FNS \
    /* x/misc series */ \
    IEL_BACKEND_FNS_ITER(xfeat) \
    IEL_BACKEND_FNS_ITER(xcntl) \
    IEL_BACKEND_FNS_ITER(xinit) \
    IEL_BACKEND_FNS_ITER(xtdwn)

#define IEL_BACKEND_NEW_FNS

#define IEL_BACKEND_FNS \
    IEL_BACKEND_FP_FNS \
    IEL_BACKEND_F_FNS \
    IEL_BACKEND_S_FNS \
    IEL_BACKEND_E_FNS \
    IEL_BACKEND_L_FNS \
    IEL_BACKEND_X_FNS \
    IEL_BACKEND_NEW_FNS

struct iel_vtable_st {
    /* Must all be non-null */
#define IEL_BACKEND_FNS_ITER(name) \
    iel_fn_##name *p_##name;

    IEL_BACKEND_FNS
#undef IEL_BACKEND_FNS_ITER
};

/* Indicates that the backend is available.
 * When unset in the cap variable, it means the backend will not be available.
 * The result of xfeat() is accurate if this bit is set.
 * The vtsetup() function could set this bit if it is certain.
 * If lnew() succeeds, this bit must be set.
 */
#define IEL_FEAT_AVAIL (1ULL << 63)
/* Indicates availability of the flag IEL_FLAG_ETIME_MICROS */
#define IEL_FEAT_ETIME_MICROS (1ULL << 62)

/* Applies to: etime
 * Available when: feature flag IEL_FEAT_ETIME_MICROS is set
 * Changes the unit of time to wait to microseconds instead of milliseconds (the default).
 */
#define IEL_FLAG_ETIME_MICROS (1ULL << 63)
/* Applies to: fp series, f series, s series, etime
 * Available when: false
 * Makes the task multishot
 * TODO: allow cancelling? and implementation
 */
#define IEL_FLAG_MULTISHOT (1ULL << 62)

#endif /* ifndef IEL_BACKENDS_H_ */
