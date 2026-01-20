#if defined(TAGH_BITS) && defined(TP_DO_SX) && defined(IDENTSFX)

#define TAG_IN_PLACE 1

#define CAT0(x,y) x ## y
#define CAT(x,y) CAT0(x, y)
#define IDENT(x) CAT(x, IDENTSFX)
#define LA_BITS (64 - TAGH_BITS)

#include <iel/tagptr.h>
#include <iel/config.h>

static inline IEL_FNATTR_NODISCARD
struct iel_tp_untag_st IDENT(utp) (void const *ptr, size_t al) {
    IEL_STATTR_ASSUME(!(al & (al - 1)));
    uintptr_t ptrval = (uintptr_t)ptr;
#if TAG_IN_PLACE
    uintmax_t const tag =
        (ptrval >> LA_BITS) |  // tagh
        ((ptrval & (uintptr_t)(al - 1)) << TAGH_BITS);  // tagl

    // clear tagl
    ptrval &= ~(uintptr_t)(al - 1);

#if TP_DO_SX
    // sign extend to fill tagh
    ptrval <<= TAGH_BITS;
    // XXX: assumes reinterpretation u->i conversion + two's complement
    ptrval = IEL_ASHR_U((intptr_t)ptrval, TAGH_BITS, uintptr_t);
#else
    ptrval &= ((1ULL << LA_BITS) - 1);
#endif

#else  // TODO: remove this branch
    uintmax_t const tag = ptrval & (((uintptr_t)al << TAGH_BITS) - 1);
#if TP_DO_SX
    // XXX: assumes reinterpretation u->i conversion + two's complement
    ptrval = IEL_ASHR_U((intptr_t)ptrval, TAGH_BITS, uintptr_t);
#else
    ptrval >>= TAGH_BITS;
#endif
    ptrval &= ~(uintptr_t)(al - 1);
#endif
    return (struct iel_tp_untag_st) {
        .tag = tag,
        .ptr = (void *)ptrval,
    };
}

static inline IEL_FNATTR_NODISCARD
void *IDENT(tp)(void const *ptr, size_t al, uintmax_t tag) {
    IEL_STATTR_ASSUME(!(al & (al - 1)));
    IEL_STATTR_ASSUME(!((uintptr_t)ptr & (al - 1)));
    uintptr_t ptrval = (uintptr_t)ptr;
#if TAG_IN_PLACE
    ptrval &= ((1ULL << LA_BITS) - 1);
    uintptr_t tagh = (tag & ((1ULL << TAGH_BITS) - 1)) << LA_BITS;
    uintmax_t tagl = tag >> TAGH_BITS;
    ptrval |= tagl | tagh;
#else  // TODO: remove this branch
    ptrval <<= TAGH_BITS;
    ptrval |= tag;
#endif
    return (void *)ptrval;
}

#undef CAT0
#undef CAT
#undef IDENT
#undef LA_BITS
#undef TAG_IN_PLACE

#undef TAGH_BITS
#undef TP_DO_SX
#undef IDENTSFX

#else
#error Do not include tp_tagh.tpl.h directly
#endif /* ifdef defined(TAGH_BITS) && defined(TP_DO_SX) && defined(IDENTSFX) */
