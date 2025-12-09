/* A simple binary min-heap implementation, gurantees NO thread-safety */
#include <stddef.h>
#include <stdlib.h>

#include <iel/arg.h>
#include <iel/minheap.h>

struct iel_mh_node_st {
    union iel_arg_un v;
    iel_mh_elem k;
};

int iel_mh_init(struct iel_mh_st *mh, iel_mh_sz initlen) {
    mh->arr = malloc(initlen * sizeof(struct iel_mh_node_st));
    if (!mh->arr)
        return -1;
    mh->entries = 0;
    mh->maxentries = initlen;
    return 0;
}

void iel_mh_del(struct iel_mh_st *mh) {
    free(mh->arr);
    mh->arr = NULL;
}

#define IEL_MH_HASIDX(mh, idx) (mh->entries > (idx))
/* precondition: IEL_MH_HASIDX(mh, idx0) && IEL_MH_HASIDX(mh, idx1) */
#define IEL_MH_SWAP(mh, idx0, idx1) \
    do { \
        union iel_arg_un v; \
        iel_mh_elem k; \
        k = (mh)->arr[idx0].k; \
        v = (mh)->arr[idx0].v; \
        (mh)->arr[idx0].k = (mh)->arr[idx1].k; \
        (mh)->arr[idx0].v = (mh)->arr[idx1].v; \
        (mh)->arr[idx1].k = k; \
        (mh)->arr[idx1].v = v; \
    } while (0)
#define IEL_MH_LE(mh, idx0, idx1) (IEL_MH_ELEM_CMP_LE(((mh)->arr[idx0].k), ((mh)->arr[idx1].k)))
#define IEL_MH_NE(mh, idx0, idx1) (IEL_MH_ELEM_CMP_NE(((mh)->arr[idx0].k), ((mh)->arr[idx1].k)))

#define IEL_MH_ROOT 0
#define IEL_MH_LAST(mh) ((iel_mh_idx)((mh)->entries) - 1)
#define IEL_MH_LEFTC(idx) (((idx) << 1) + 1)
#define IEL_MH_RIGHTC(idx) (((idx) << 1) + 2)
/* precondition: idx != IEL_MH_ROOT */
#define IEL_MH_PAR(idx) (((idx) - 1) >> 1)
/* precondition: idx != IEL_MH_ROOT */
#define IEL_MH_PEER(idx) (((idx) & 1) ? (idx) + 1 : (idx) - 1)

/* sift-up
 * time: O(log (mh->entries)) worst / O(1) best
 * precondition: IEL_MH_HASIDX(mh, idx) && idx != IEL_MH_ROOT
 */
static inline
void iel_mh_sup(struct iel_mh_st const *mh, iel_mh_idx idx) {
    iel_mh_idx cur = idx;
    iel_mh_idx par = IEL_MH_PAR(cur);
    while (!IEL_MH_LE(mh, par, /*<=*/cur)) {
        IEL_MH_SWAP(mh, cur, par);
        if (par == IEL_MH_ROOT) {
            break;
        }
        cur = par;
        par = IEL_MH_PAR(par);
    }
}

/* sift-down
 * time: O(log (mh->entries)) worst / O(1) best
 * precondition: IEL_MH_HASIDX(mh, idx) && \
 *     (!IEL_MH_HASIDX(mh, IEL_MH_LEFTC(idx)) || IEL_MH_LE(mh, IEL_MH_LEFTC(idx), idx)) && \
 *     (!IEL_MH_HASIDX(mh, IEL_MH_RIGHTC(idx)) || IEL_MH_LE(mh, IEL_MH_RIGHTC(idx), idx))
 * (this is true after a pop-min, so we can do-while to save one branch)
 */
static inline
void iel_mh_sdn(struct iel_mh_st const *mh, iel_mh_idx idx) {
    iel_mh_idx cur = idx;
    do {
        iel_mh_idx n_lc = IEL_MH_LEFTC(cur), n_rc = IEL_MH_RIGHTC(cur);
        unsigned char has_l = IEL_MH_HASIDX(mh, n_lc), has_r = IEL_MH_HASIDX(mh, n_rc);
        iel_mh_idx swap_with =
            has_l
            ? has_r
                ? IEL_MH_LE(mh, n_lc, n_rc) ? n_lc : n_rc
                : n_lc
            : has_r
                ? n_rc
                : cur;
        if (swap_with != cur && IEL_MH_NE(mh, swap_with, cur)) {
            IEL_MH_SWAP(mh, cur, swap_with);
            cur = swap_with;
        }
        else
            break;
    }
    while (1);
}

int iel_mh_ins1(struct iel_mh_st *mh, iel_mh_elem k, union iel_arg_un v) {
    iel_mh_idx idx = mh->entries;
    iel_mh_sz new_sz = mh->entries + 1;
    if (new_sz > mh->maxentries) {  /* extend */
        iel_mh_sz new_maxsz = mh->maxentries << 1;
        struct iel_mh_node_st *p = realloc(mh->arr, new_maxsz * sizeof(struct iel_mh_node_st));
        if (!p)
            return -1;
        mh->arr = p;
        mh->maxentries = new_maxsz;
    }
    mh->arr[idx].k=k;
    mh->arr[idx].v=v;
    mh->entries = new_sz;
    if (idx != IEL_MH_ROOT)
        iel_mh_sup(mh, idx);
    return 0;
}

/* TODO: batch-insert/make-heap */

int iel_mh_pop(struct iel_mh_st *mh, iel_mh_elem *pk, union iel_arg_un *pv) {
    if (!mh->entries)
        return -1;
    *pk = mh->arr[IEL_MH_ROOT].k;
    *pv = mh->arr[IEL_MH_ROOT].v;
    IEL_MH_SWAP(mh, IEL_MH_ROOT, IEL_MH_LAST(mh));
    --mh->entries;
    if (mh->entries)
        iel_mh_sdn(mh, IEL_MH_ROOT);
    return 0;
}

/* TODO: modify/replace */
