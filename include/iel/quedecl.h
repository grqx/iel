#ifndef IEL_QUEDECL_H_
#define IEL_QUEDECL_H_

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

typedef void *iel_que_gmap;

typedef size_t iel_que_sz;
typedef size_t iel_que_idx;

typedef uint_least32_t iel_que_offset;

struct iel_que_st {
    /* array of pointers to chunks */
    iel_que_gmap map;
    /* map entries allocated */
    iel_que_sz mapcap;
    /* start/end chunk index in map */
    iel_que_idx chunk_s, chunk_e;
    /* start/end offset in chunk */
    iel_que_offset os_s, os_e;
};

#endif /* ifndef IEL_QUEDECL_H_ */
