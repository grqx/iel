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
#define IEL_PF_SOCKET_IS_FILE 1
#define IEL_PF_FD_INVAL ((iel_pf_fd) -1)
#define IEL_PF_FD_R_INVAL ((iel_pf_fd_r) -1)
#define IEL_PF_SOCKFD_INVAL ((iel_pf_sockfd) -1)
#define IEL_PF_SOCKFD_R_INVAL ((iel_pf_sockfd_r) -1)

typedef off_t iel_pf_pos;

/* int(WinSock, BSD original), size_t(later POSIX), or socklen_t (POSIX 2001) */
typedef socklen_t iel_pf_socklen;
typedef void iel_pf_sockaf;
#endif  /* ifdef _WIN32 */


#ifdef IEL_PF_SOCKET_IS_FILE
typedef iel_pf_fd iel_pf_sockfd;
#else
/* require the type to exist */
typedef iel_pf_sockfd iel_pf_sockfd;
#endif

typedef iel_pf_fd iel_pf_fd_r;
typedef iel_pf_sockfd iel_pf_sockfd_r;

typedef struct {
    iel_pf_fd raw;
    iel_pf_fd_r reg;
} iel_pf_reg_fd;

#ifdef IEL_PF_SOCKET_IS_FILE
typedef iel_pf_reg_fd iel_pf_reg_sockfd;
#else
typedef struct {
    iel_pf_sockfd raw;
    iel_pf_sockfd_r reg;
} iel_pf_reg_sockfd;
#endif


#define IEL_IOV_ptr_of(iov) ( (iov) . IEL_IOV_M_ptr )
#define IEL_IOV_len_of(iov) ( (iov) . IEL_IOV_M_len )
#define IEL_IOV_M(iov, mem) IEL_IOV_ ## mem ## _of (iov)
#define IEL_IOV_offsetof_ptr offsetof(iel_pf_iov, IEL_IOV_M_ptr)
#define IEL_IOV_offsetof_len offsetof(iel_pf_iov, IEL_IOV_M_len)
#define IEL_IOV_offsetof(mem) IEL_IOV_offsetof_ ## mem

#endif /* ifndef IEL_PLATFORM_H_ */
