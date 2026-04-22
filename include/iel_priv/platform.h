#ifndef IEL_PRIV_PLATFORM_H_
#define IEL_PRIV_PLATFORM_H_

#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
#define IEL_PF_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define IEL_PF_AARCH64 1
#endif

#ifndef IEL_PF_X64
#define IEL_PF_X64 0
#endif

#ifndef IEL_PF_AARCH64
#define IEL_PF_AARCH64 0
#endif

#include <stdint.h>

#ifdef UINTPTR_MAX
#define IEL_PF_TAGL 1
#else
#define IEL_PF_TAGL 0
#endif

#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
// one extra bit(0) can be used for tagging in 64 bit mode userspace
#define IEL_PF_MMLOWUSER 1
#else
#define IEL_PF_MMLOWUSER 0
#endif

#endif /* ifndef IEL_PRIV_PLATFORM_H_ */
