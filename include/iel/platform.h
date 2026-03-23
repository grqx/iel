#ifndef IEL_PLATFORM_H_
#define IEL_PLATFORM_H_

#ifdef _WIN32
#error TODO
#else
#include <sys/types.h>  /* off_t */
#include <sys/socket.h>  /* iovec, socklen_t */

typedef struct iovec iel_pf_iov;
#define IEL_IOV_M_ptr iov_base
#define IEL_IOV_M_len iov_len
typedef int iel_pf_fd;
typedef int iel_pf_sockfd;
typedef off_t iel_pf_pos;

/* int(WinSock, BSD original), size_t(later POSIX), or socklen_t (POSIX 2001) */
typedef socklen_t iel_pf_socklen;
typedef sa_family_t iel_pf_sockaf;
#endif  /* ifdef _WIN32 */

#define IEL_IOV_ptr_of(iov) ( (iov) . IEL_IOV_M_ptr )
#define IEL_IOV_len_of(iov) ( (iov) . IEL_IOV_M_len )
#define IEL_IOV_M(iov, mem) IEL_IOV_ ## mem ## _of (iov)
#define IEL_IOV_offsetof_ptr offsetof(iel_pf_iov, IEL_IOV_M_ptr)
#define IEL_IOV_offsetof_len offsetof(iel_pf_iov, IEL_IOV_M_len)
#define IEL_IOV_offsetof(mem) IEL_IOV_offsetof_ ## mem

#endif /* ifndef IEL_PLATFORM_H_ */
