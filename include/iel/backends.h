#ifndef IEL_BACKENDS_H_
#define IEL_BACKENDS_H_

#include <stdalign.h>
#include <stddef.h>

#include <iel/platform.h>
#include <iel/config.h>
#include <iel/arg.h>

struct iel_vtable_st;

typedef ptrdiff_t iel_taskres;
typedef void (iel_cbfn)(void *, iel_taskres);
typedef iel_cbfn *iel_cb;
/* intrusive struct, we can put OVERLAPPED here if needed later for IOCP */
struct iel_cb_base {
    iel_cb cb;
};
/* TODO: add a flag for max-aligned callbacks(incompatible ABI!) */
#if 0
#define IEL_CB_ALIGN alignof(max_align_t)
#else
#define IEL_CB_ALIGN alignof(iel_cb)
#endif
#define IEL_CB_BASE alignas(IEL_CB_ALIGN) struct iel_cb_base

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

// The backend will be available, p_lnew is expected to succeed
#define IEL_VTSETUP_RET_AVAIL ((unsigned char) '\x00')
// The backend will be unavailable, p_lnew must always fail
#define IEL_VTSETUP_RET_UNAVAIL ((unsigned char) '\x01')
// The backend's availability is unknown, p_lnew may or may not succeed
#define IEL_VTSETUP_RET_UNSURE ((unsigned char) '\x02')
// The user made a logic error when calling the vtsetup function
#define IEL_VTSETUP_RET_ERROR ((unsigned char) '\xff')

typedef unsigned char (iel_fn_vtsetup)(struct iel_vtable_st *);

// TODO: REG_BUF, REG_FD

/* fp/FilePosition series */
/* FilePosition::Read */
typedef void *(iel_fn_fpr)(void *ctx, iel_pf_fd_r fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, void *cbp);
/* FilePosition::ReadVector */
typedef void *(iel_fn_fprv)(void *ctx, iel_pf_fd_r fd, iel_pf_iov *iovecs, size_t iovcnt, iel_pf_pos offset, union iel_arg_un flags, void *cbp);
/* FilePosition::Write */
typedef void *(iel_fn_fpw)(void *ctx, iel_pf_fd_r fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, void *cbp);
/* FilePosition::WriteVector */
typedef void *(iel_fn_fpwv)(void *ctx, iel_pf_fd_r fd, iel_pf_iov *iovecs, size_t iovcnt, iel_pf_pos offset, union iel_arg_un flags, void *cbp);

/* f/File series */
/* File::Read */
typedef void *(iel_fn_fr)(void *ctx, iel_pf_fd_r fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
/* File::ReadVector */
typedef void *(iel_fn_frv)(void *ctx, iel_pf_fd_r fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);
/* File::Write */
typedef void *(iel_fn_fw)(void *ctx, iel_pf_fd_r fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
/* File::WriteVector */
typedef void *(iel_fn_fwv)(void *ctx, iel_pf_fd_r fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);

/* s/Socket series */
/* Socket::Accept
 * Completes when read-ready.
 */
typedef void *(iel_fn_sa)(void *ctx, iel_pf_sockfd_r fd, iel_pf_sockaf *addr_out, iel_pf_socklen *addrlen_out, union iel_arg_un flags, void *cbp);
/* Socket::Connect
 * Completes when write-ready.
 */
typedef void *(iel_fn_sc)(void *ctx, iel_pf_sockfd_r fd, iel_pf_sockaf *addr, iel_pf_socklen addrlen, union iel_arg_un flags, void *cbp);

/* Socket::Read */
typedef void *(iel_fn_sr)(void *ctx, iel_pf_sockfd_r fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
/* Socket::ReadVector */
typedef void *(iel_fn_srv)(void *ctx, iel_pf_sockfd_r fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);
/* Socket::Write */
typedef void *(iel_fn_sw)(void *ctx, iel_pf_sockfd_r fd, const unsigned char *buf, size_t count, union iel_arg_un flags, void *cbp);
/* Socket::WriteVector */
typedef void *(iel_fn_swv)(void *ctx, iel_pf_sockfd_r fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, void *cbp);

/* e/Execute series
 * TODO: libuv-ish idle(don't call it idle)/prepare callback queue
 */
/* Execute::Timer */
typedef void *(iel_fn_etime)(void *ctx, unsigned long long time, union iel_arg_un flags, void *cbp);
/* Execute::Soon */
typedef void *(iel_fn_esoon)(void *ctx, union iel_arg_un flags, void *cbp);

/* l/Loop series */
/* Loop::GetCtxSize */
typedef size_t (iel_fn_lsize)(void);
/* Loop::New */
typedef int (iel_fn_lnew)(void *ctx, long max_files, long max_bufs, union iel_arg_un flags);
/* Loop::Delete */
typedef void (iel_fn_ldel)(void *ctx);
/* Loop::RunOnce
 * This might call more than one pending callbacks
 */
typedef int (iel_fn_lrun1)(void *ctx, union iel_arg_un flags);

/* x/Misc series */
/* Misc::GetFeatures
 * xfeat() works in two modes:
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
/* Misc::Control */
typedef union iel_arg_un (iel_fn_xcntl)(void *ctx, unsigned short op, union iel_arg_un arg0, union iel_arg_un arg1);
/* Misc::AsyncRegisterResource */
typedef iel_taskres (iel_fn_xreg)(void *ctx, unsigned char opcode, void const * /*Nullable*/ IEL_CQUAL_RESTRICT in, void * IEL_CQUAL_RESTRICT out, size_t nr_args, union iel_arg_un flags);
/* Misc::Initialize */
typedef void (iel_fn_xinit)(union iel_arg_un flags);
/* Misc::TearDown */
typedef void (iel_fn_xtdwn)(union iel_arg_un flags);

#define IEL_BACKEND_FP_FNS \
    IEL_BACKEND_FNS_ITER(fpr) \
    IEL_BACKEND_FNS_ITER(fprv) \
    IEL_BACKEND_FNS_ITER(fpw) \
    IEL_BACKEND_FNS_ITER(fpwv)

#define IEL_BACKEND_F_FNS \
    IEL_BACKEND_FNS_ITER(fr) \
    IEL_BACKEND_FNS_ITER(frv) \
    IEL_BACKEND_FNS_ITER(fw) \
    IEL_BACKEND_FNS_ITER(fwv)

#define IEL_BACKEND_S_FNS \
    IEL_BACKEND_FNS_ITER(sa) \
    IEL_BACKEND_FNS_ITER(sc) \
    IEL_BACKEND_FNS_ITER(sr) \
    IEL_BACKEND_FNS_ITER(srv) \
    IEL_BACKEND_FNS_ITER(sw) \
    IEL_BACKEND_FNS_ITER(swv)

#define IEL_BACKEND_E_FNS \
    IEL_BACKEND_FNS_ITER(etime) \
    IEL_BACKEND_FNS_ITER(esoon)

#define IEL_BACKEND_L_FNS \
    IEL_BACKEND_FNS_ITER(lnew) \
    IEL_BACKEND_FNS_ITER(ldel) \
    IEL_BACKEND_FNS_ITER(lrun1) \
    IEL_BACKEND_FNS_ITER(lsize)

#define IEL_BACKEND_X_FNS \
    IEL_BACKEND_FNS_ITER(xfeat) \
    IEL_BACKEND_FNS_ITER(xreg) \
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
/* Indicates availability of the flag IEL_FLAG_REQLNK */
#define IEL_FEAT_REQLNK (1ULL << 61)
#define IEL_FEAT_NOREG_HANDLE (1ULL << 60)

/* Applies to: etime
 * Available when: feature flag IEL_FEAT_ETIME_MICROS is set
 * Changes the unit of time to wait to microseconds instead of milliseconds (the default).
 */
#define IEL_FLAG_ETIME_MICROS (1ULL << 63)
/* Applies to: esoon
 * Available when: always
 * Calls the callback on next loop. (Default: execute as soon as possible)
 */
#define IEL_FLAG_ESOON_NEXT (1ULL << 63)
/* Applies to: fp series, f series, s series, etime
 * Available when: feature flag IEL_FEAT_REQLNK is set
 * Links the current task with the next one.
 * The user_data argument will be ignored.
 * XXX: esoon is not supported
 */
#define IEL_FLAG_REQLNK (1ULL << 62)
#define IEL_FLAG_NOREG_HANDLE (1ULL << 61)

/* typ is fd or sockfd */
#define IEL_BE_REGF(reg_st) \
    ( (reg_st).reg != IEL_PF_FD_R_INVAL ? (reg_st).reg : (reg_st).raw )
#define IEL_BE_REGS(reg_st) \
    ( (reg_st).reg != IEL_PF_SOCKFD_R_INVAL ? (reg_st).reg : (reg_st).raw )

#define IEL_BE_REGF_FLAG(reg_st) \
    ( (reg_st).reg != IEL_PF_FD_R_INVAL ? 0 : IEL_FLAG_NOREG_HANDLE )
#define IEL_BE_REGS_FLAG(reg_st) \
    ( (reg_st).reg != IEL_PF_SOCKFD_R_INVAL ? 0 : IEL_FLAG_NOREG_HANDLE )

#if 0
/* Applies to: fp series, f series, s series, etime
 * Available when: FIXME: false
 * Makes the task multishot
 * TODO: allow cancelling? and implementation
 */
#define IEL_FLAG_MULTISHOT (1ULL << 62)
#endif

/* xreg() opcode for registering files */
#define IEL_XREG_FILES ((unsigned char) '\x00')
#define IEL_XREG_SOCKETS ((unsigned char) '\x01')
#define IEL_XREG_DE_FILES ((unsigned char) '\x02')
#define IEL_XREG_DE_SOCKETS ((unsigned char) '\x03')

#define IEL_FLAG_XREG_DRG (1ULL)
#define IEL_FLAG_XREG_DEL (2ULL)

#endif /* ifndef IEL_BACKENDS_H_ */
