#include <iel/config.h>
#include <iel/tagptr.h>
#include <iel_priv/platform.h>

typedef IEL_FNATTR_NODISCARD
struct iel_tp_untag_st (*utp_fnptr)(void const *ptr, size_t al);
typedef IEL_FNATTR_NODISCARD
void *(*tp_fnptr)(void const *ptr, size_t al, uintmax_t tag);
typedef IEL_FNATTR_NODISCARD
uintmax_t (*maxtag_fnptr)(size_t al);

#if IEL_PF_TAGL

static inline IEL_FNATTR_NODISCARD
struct iel_tp_untag_st utp_l(void const *ptr, size_t al) {
    IEL_STATTR_ASSUME(!(al & (al - 1)));
    return (struct iel_tp_untag_st) {
        .tag = (uintptr_t)ptr & (al - 1),
        .ptr = (void *)((uintptr_t)ptr & ~(uintptr_t)(al - 1)),
    };
}

static inline IEL_FNATTR_NODISCARD
void *tp_l(void const *ptr, size_t al, uintmax_t tag) {
    IEL_STATTR_ASSUME(!(al & (al - 1)));
    IEL_STATTR_ASSUME(!((uintptr_t)ptr & (al - 1)));
    return (void *)((uintptr_t)ptr | tag);
}

static inline IEL_FNATTR_NODISCARD
uintmax_t maxtag_l(size_t al) {
    IEL_STATTR_ASSUME(!(al & (al - 1)));
    return al;
}

#endif

#if IEL_PF_X64 && (defined(__GNUC__) || defined(_MSC_VER))

#include <assert.h>
#include <stdatomic.h>
static_assert(IEL_PF_TAGL, "x86-64 must allow low-bits tagging");

typedef unsigned CPUI[4];
#ifdef __GNUC__
#include <cpuid.h>
#define getCPUI(fn, i) __cpuid(fn, i[0], i[1], i[2], i[3])
#elif defined(_MSC_VER)
#include <intrin.h>
#define getCPUI(fn, i) __cpuid(i, fn)
#endif

#if IEL_PF_MMLOWUSER
#define LA_BITS 47
#define TP_DO_SX 0
#else
#define LA_BITS 48
#define TP_DO_SX 1
#endif
#define IDENTSFX _la48
#include <iel_priv/tp_tagh.tpl.h>
#if IEL_PF_MMLOWUSER
#define LA_BITS 56
#define TP_DO_SX 0
#else
#define LA_BITS 57
#define TP_DO_SX 1
#endif
#define IDENTSFX _la57
#include <iel_priv/tp_tagh.tpl.h>

static _Atomic(utp_fnptr) utp_v;
static _Atomic(tp_fnptr) tp_v;
static _Atomic(maxtag_fnptr) maxtag_v;

#define ld_rlx(p) atomic_load_explicit(p, memory_order_relaxed)
#define st_rlx(p,v) atomic_store_explicit(p, v, memory_order_relaxed)

IEL_FNATTR_NODISCARD
struct iel_tp_untag_st iel_tp_untag(void const *ptr, size_t al) {
    return ld_rlx(&utp_v)(ptr, al);
}

IEL_FNATTR_NODISCARD
void *iel_tp_tag(void const *ptr, size_t al, uintmax_t tag) {
    return ld_rlx(&tp_v)(ptr, al, tag);
}

IEL_FNATTR_NODISCARD
uintmax_t iel_tp_max(size_t al) {
    return ld_rlx(&maxtag_v)(al);
}

void iel_tp_init(void) {
    CPUI cpui;
    unsigned char la_bits;
    getCPUI(0x80000000, cpui);
    if (cpui[0] < 0x80000008)
        goto tagl;
    getCPUI(0x80000008, cpui);
    la_bits = (cpui[0] >> 8) & 0xff;
    // Always assume LA57 where the hardware is capable of using 5LP
    // Could the kernel toggle CR4.LA57 live?
    if (la_bits == 48) {
        st_rlx(&tp_v, &tp_la48);
        st_rlx(&utp_v, &utp_la48);
        st_rlx(&maxtag_v, &maxtag_la48);
    } else if (la_bits == 57) {
        st_rlx(&tp_v, &tp_la57);
        st_rlx(&utp_v, &utp_la57);
        st_rlx(&maxtag_v, &maxtag_la57);
    } else {
tagl:;
        st_rlx(&tp_v, &tp_l);
        st_rlx(&utp_v, &utp_l);
        st_rlx(&maxtag_v, &maxtag_l);
    }
}

#elif 0 // IEL_PF_AARCH64

// TODO: disabled, need to handle PAC collision

#include <iel_priv/generated/feat.h>
#if IEL_AA64_HAVE_MTE
#define LA_BITS 60
#define TP_DO_SX 0
#else
#define LA_BITS 56
#if IEL_PF_MMLOWUSER
#define TP_DO_SX 0
#else
#define TP_DO_SX 1
#endif
#endif
#define IDENTSFX _topb
#include <iel_priv/tp_tagh.tpl.h>

void iel_tp_init(void) {}

IEL_FNATTR_NODISCARD
uintmax_t iel_tp_max(size_t al) {
    return maxtag_topb(al);
}

IEL_FNATTR_NODISCARD
struct iel_tp_untag_st iel_tp_untag(void const *ptr, size_t al) {
    return utp_topb(ptr, al);
}

IEL_FNATTR_NODISCARD
void *iel_tp_tag(void const *ptr, size_t al, uintmax_t tag) {
    return tp_topb(ptr, al, tag);
}

#elif IEL_PF_TAGL

void iel_tp_init(void) {}

IEL_FNATTR_NODISCARD
uintmax_t iel_tp_max(size_t al) { return maxtag_l(al); }

IEL_FNATTR_NODISCARD
struct iel_tp_untag_st iel_tp_untag(void const *ptr, size_t al) {
    return utp_l(ptr, al);
}

IEL_FNATTR_NODISCARD
void *iel_tp_tag(void const *ptr, size_t al, uintmax_t tag) {
    return tp_l(ptr, al, tag);
}

#else  // no tagged pointers
void iel_tp_init(void) {}

IEL_FNATTR_NODISCARD
uintmax_t iel_tp_max(size_t al) { return 1; }

IEL_FNATTR_NODISCARD
struct iel_tp_untag_st iel_tp_untag(void const *ptr, size_t al) {
    return (struct iel_tp_untag_st) {
        .tag = 0,
        .ptr = ptr,
    };
}

IEL_FNATTR_NODISCARD
void *iel_tp_tag(void const *ptr, size_t al, uintmax_t tag) { return ptr; }

#endif
