#ifndef IEL_PLATFORM_H_
#define IEL_PLATFORM_H_

#ifdef _WIN32
#error TODO
#else
#include <sys/types.h>  /* off_t */
#include <sys/socket.h>  /* iovec, socklen_t */

typedef struct iovec iel_pf_iov;
#define IEL_PF_IOVEC_M_ptr iov_base
#define IEL_PF_IOVEC_M_len iov_len
typedef int iel_pf_fd;
typedef int iel_pf_sockfd;
typedef off_t iel_pf_pos;

/* int(WinSock, BSD original), size_t(later POSIX), or socklen_t (POSIX 2001) */
typedef socklen_t iel_pf_socklen;
typedef sa_family_t iel_pf_sockaf;
#endif  /* ifdef _WIN32 */


#define IEL_PF_IOV_GET(iov, m) (iov.IEL_PF_IOVEC_M_ ## m)

#define IEL_PF_IOV_PTR(iov) IEL_PF_IOV_GET(iov, ptr)
#define IEL_PF_IOV_LEN(iov) IEL_PF_IOV_GET(iov, len)

#endif /* ifndef IEL_PLATFORM_H_ */
