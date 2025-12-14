#ifndef IEL_PTRQUE_H_
#define IEL_PTRQUE_H_

#include <stdlib.h>
#include <stdint.h>

typedef void *iel_que_elem;
typedef size_t iel_que_sz;
typedef size_t iel_que_idx;
typedef uint16_t iel_que_offset;

typedef iel_que_elem *iel_que_elem_p;
typedef iel_que_elem_p *iel_que_map;

struct iel_que_st {
    /* array of pointers to chunks */
    iel_que_map map;
    /* map entries allocated */
    iel_que_sz mapcap;
    /* start/end chunk index in map */
    iel_que_idx chunk_s, chunk_e;
    /* start/end offset in chunk */
    iel_que_offset os_s, os_e;
};

int iel_que_init(struct iel_que_st *que, iel_que_sz initcap_map);
iel_que_sz iel_que_size(struct iel_que_st *que);
int iel_que_push1(struct iel_que_st *que, iel_que_elem v);
int iel_que_pop1(struct iel_que_st *que, iel_que_elem *vout);
void iel_que_del(struct iel_que_st *que);
void iel_que_qtrim(struct iel_que_st *que);
void iel_que_trim(struct iel_que_st *que);

#endif /* ifndef IEL_PTRQUE_H_ */
