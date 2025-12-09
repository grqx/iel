#ifndef IEL_BACKENDS_H_
#define IEL_BACKENDS_H_

#include <iel/platform.h>
#include <iel/arg.h>

struct iel_cb_base;
typedef void (*iel_cb)(struct iel_cb_base *, int);
typedef struct iel_cb_base {
    iel_cb cb;
} *iel_cbp;

// The backend will be available, p_lnew must always succeed unless there is a logic error in user code
#define IEL_VTSETUP_RET_AVAIL ((unsigned char) '\x00')
// The backend will be unavailable, p_lnew must always fail
#define IEL_VTSETUP_RET_UNAVAIL ((unsigned char) '\x01')
// The backend's availability is unknown, p_lnew may or may not succeed
#define IEL_VTSETUP_RET_UNSURE ((unsigned char) '\x02')
// The user made a logic error when calling the vtsetup function
#define IEL_VTSETUP_RET_ERROR ((unsigned char) '\xff')

typedef void (*iel_fnptr_fpr)(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_fprv)(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovlen, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_fpw)(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_fpwv)(void *ctx, iel_pf_fd fd, iel_pf_iov *iovecs, size_t iovlen, iel_pf_pos offset, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_fr)(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_frv)(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_fw)(void *ctx, iel_pf_fd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_fwv)(void *ctx, iel_pf_fd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, iel_cbp cbp);

typedef void (*iel_fnptr_sr)(void *ctx, iel_pf_sockfd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_srv)(void *ctx, iel_pf_sockfd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_sw)(void *ctx, iel_pf_sockfd fd, const unsigned char *buf, size_t count, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_swv)(void *ctx, iel_pf_sockfd fd, iel_pf_iov *iov, size_t iovcnt, union iel_arg_un flags, iel_cbp cbp);

typedef void (*iel_fnptr_etime)(void *ctx, unsigned long long millis, union iel_arg_un flags, iel_cbp cbp);
typedef void (*iel_fnptr_esoon)(void *ctx, union iel_arg_un flags, iel_cbp cbp);

typedef size_t (*iel_fnptr_lsize)(void);
typedef int (*iel_fnptr_lnew)(void *ctx, union iel_arg_un flags);
typedef void (*iel_fnptr_ldel)(void *ctx);
typedef int (*iel_fnptr_lrun1)(void *ctx, union iel_arg_un flags);

typedef unsigned long long (*iel_fnptr_xfeat)(void *ctx, union iel_arg_un flags);
typedef unsigned long long (*iel_fnptr_xcntl)(void *ctx, unsigned char op, unsigned long long arg0, unsigned long long arg1);
typedef void (*iel_fnptr_xinit)(union iel_arg_un flags);
typedef void (*iel_fnptr_xtdwn)(union iel_arg_un flags);

struct iel_vtable_st {
    iel_fnptr_fpr p_fpr;
    iel_fnptr_fprv p_fprv;
    iel_fnptr_fpw p_fpw;
    iel_fnptr_fpwv p_fpwv;

    iel_fnptr_fr p_fr;
    iel_fnptr_frv p_frv;
    iel_fnptr_fw p_fw;
    iel_fnptr_fwv p_fwv;

    iel_fnptr_sr p_sr;
    iel_fnptr_srv p_srv;
    iel_fnptr_sw p_sw;
    iel_fnptr_swv p_swv;

    iel_fnptr_etime p_etime;
    iel_fnptr_esoon p_esoon;

    iel_fnptr_lnew p_lnew;
    iel_fnptr_ldel p_ldel;
    iel_fnptr_lrun1 p_lrun1;
    iel_fnptr_lsize p_lsize;

    iel_fnptr_xfeat p_xfeat;
    iel_fnptr_xcntl p_xcntl;
    iel_fnptr_xinit p_xinit;  // Nullable
    iel_fnptr_xtdwn p_xtdwn;  // Nullable
};

#endif /* ifndef IEL_BACKENDS_H_ */
