#ifndef IEL_MINHEAP_H_
#define IEL_MINHEAP_H_
#include <stdint.h>

#include <iel/arg.h>

#define IEL_MH_ELEM_CMP_LE(x, y) x <= y
#define IEL_MH_ELEM_CMP_NE(x, y) x != y
typedef unsigned long long iel_mh_elem;

typedef uint32_t iel_mh_sz;
typedef uint32_t iel_mh_idx;

struct iel_mh_node_st;
struct iel_mh_st {
    struct iel_mh_node_st *arr;
    iel_mh_sz entries, maxentries;
};

int iel_mh_init(struct iel_mh_st *mh, iel_mh_sz initlen);
void iel_mh_del(struct iel_mh_st *mh);
int iel_mh_ins1(struct iel_mh_st *mh, iel_mh_elem k, union iel_arg_un v);
int iel_mh_pop(struct iel_mh_st *mh, iel_mh_elem *pk, union iel_arg_un *pv);

#endif /* ifndef IEL_MINHEAP_H_ */
