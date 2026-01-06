#ifndef IEL_ERRNO_H_
#define IEL_ERRNO_H_

#include <iel/config.h>

typedef int iel_error;

#ifdef _WIN32
#error Unsupported
#else
#define IEL_EPERM 1
#define IEL_ENOENT 2
#define IEL_EINTR 4
#define IEL_EIO 5
#define IEL_EBADF 9
#define IEL_ENOMEM 12
#define IEL_EACCES 13
#define IEL_EINVAL 22
#define IEL_ECANCELED 125
#endif

IEL_STABLE_API IEL_FNATTR_NODISCARD IEL_FNATTR_CONST
iel_error *iel_errno_getloc(void);

#define iel_errno (*iel_errno_getloc())

#endif /* ifndef IEL_ERRNO_H_ */
