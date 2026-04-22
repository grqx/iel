#ifndef IEL_PRIV_BITMAP_H_
#define IEL_PRIV_BITMAP_H_

#include <iel/config.h>

#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>

typedef uint64_t iel_bm_elem;

#define IEL_BM_BITS_IN_ELEM (64)
#define IEL_BM_ELEM_MAX (UINT64_MAX)
#define IEL_BM_NPOS (SIZE_MAX)

static inline
iel_bm_elem iel_bm_bsc_(iel_bm_elem n, iel_bm_elem *out) {
    iel_bm_elem r = __builtin_ctzg(n);  // bsf
    n &= (n - 1);  // blsr
    *out = n;
    return r;
}

static inline IEL_FNATTR_NODISCARD
size_t iel_bm_scan_clear(iel_bm_elem *bm, size_t len, size_t *it) {
    size_t ch_idx = *it;
    size_t ret = IEL_BM_NPOS;
    do {
        iel_bm_elem * const out = &bm[ch_idx];
        iel_bm_elem const n = *out;

        if (n) {
            ret = ch_idx++ * IEL_BM_BITS_IN_ELEM + iel_bm_bsc_(n, out);
            break;
        }
        ++ch_idx;
    } while (ch_idx < len);
    *it = ch_idx;
    return ret;
}

static inline
void iel_bm_set(iel_bm_elem *bm, size_t len, size_t idx) {
    size_t chk_idx = idx / IEL_BM_BITS_IN_ELEM;
    size_t bit_idx = idx % IEL_BM_BITS_IN_ELEM;
    IEL_STATTR_ASSUME(chk_idx < len);
    assert(chk_idx < len);
    bm[chk_idx] |= 1UL << bit_idx;
}

#endif /* ifndef IEL_PRIV_BITMAP_H_ */
