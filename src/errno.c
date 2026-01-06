#include <iel/errno.h>

#if __STDC_VERSION__ < 202311L
#define IEL_THREAD_LOCAL _Thread_local
#else
#define IEL_THREAD_LOCAL thread_local
#endif /* if __STDC_VERSION__ < 202311L */

static IEL_THREAD_LOCAL
iel_error iel_priv_errno_v = 0;

IEL_FNATTR_NODISCARD IEL_FNATTR_CONST
iel_error *iel_errno_getloc(void) {
    return &iel_priv_errno_v;
}
