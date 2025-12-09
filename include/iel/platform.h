#ifndef IEL_PLATFORM_H_
#define IEL_PLATFORM_H_

#include <stdlib.h>

typedef struct iel_pf_iov_st {
    void *ptr;
    size_t len;
} iel_pf_iov;
typedef int iel_pf_fd;
typedef int iel_pf_sockfd;
typedef long long iel_pf_pos;

#endif /* ifndef IEL_PLATFORM_H_ */
