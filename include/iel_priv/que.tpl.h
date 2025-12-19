#ifdef IEL_QUE_TPL
// #define IEL_QUE_TPL (/*ChunkEntsBit=*/, /*MinChunks=*/, /*type=*/, /*pfx=*/, /*sfx=*/, /*api=*/)

#include <iel_priv/pp.h>
#define IEL_QUE_GETARG(n) IEL_PP_GETARG(n, IEL_QUE_TPL)

#define IEL_QUE_CHUNK_ENTS_BIT IEL_QUE_GETARG(0)
#define IEL_QUE_MINCHUNKS IEL_QUE_GETARG(1)
#define IEL_QUE_ELEM IEL_QUE_GETARG(2)
#define IEL_QUE_IDENT(x) IEL_PP_CONCAT3(IEL_QUE_GETARG(3), x, IEL_QUE_GETARG(4))
#define IEL_QUE_API IEL_QUE_GETARG(5)

#ifndef IEL_PRIV_QUE_TPL_H_
#define IEL_PRIV_QUE_TPL_H_

#include <stdlib.h>
#include <stdint.h>

typedef void *iel_que_gmap;

typedef size_t iel_que_sz;
typedef size_t iel_que_idx;
typedef uint16_t iel_que_offset;

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

#endif /* ifndef IEL_PRIV_QUE_TPL_H_ */

IEL_QUE_API
int IEL_QUE_IDENT(init) (struct iel_que_st *que, iel_que_sz initcap_map);
IEL_QUE_API
iel_que_sz IEL_QUE_IDENT(size) (struct iel_que_st const *que);
IEL_QUE_API
int IEL_QUE_IDENT(push1) (struct iel_que_st *que, IEL_QUE_ELEM v);
IEL_QUE_API
int IEL_QUE_IDENT(pop1) (struct iel_que_st *que, IEL_QUE_ELEM *vout);
IEL_QUE_API
void IEL_QUE_IDENT(del) (struct iel_que_st *que);
IEL_QUE_API
void IEL_QUE_IDENT(qtrim) (struct iel_que_st *que);
IEL_QUE_API
void IEL_QUE_IDENT(trim) (struct iel_que_st *que);

#ifdef IEL_QUE_IMPL
#include <string.h>

#define IEL_QUE_CHUNK_ENTS_MASK ((1U << IEL_QUE_CHUNK_ENTS_BIT) - 1)
#define IEL_QUE_CHUNK_SZ (sizeof(IEL_QUE_ELEM) << IEL_QUE_CHUNK_ENTS_BIT)
#define IEL_QUE_MAPOF(que) ((IEL_QUE_ELEM **) que->map)
#define IEL_QUE_MAPSZ4(cap) (cap * sizeof(IEL_QUE_ELEM *))

IEL_QUE_API
int IEL_QUE_IDENT(init) (struct iel_que_st *que, iel_que_sz initcap_map) {
    if (initcap_map < IEL_QUE_MINCHUNKS)
        initcap_map = IEL_QUE_MINCHUNKS;
    que->map = malloc(IEL_QUE_MAPSZ4(initcap_map));
    if (!que->map)
        return -1;

    que->mapcap = initcap_map;

    que->chunk_s = 0;
    que->chunk_e = 0;
    que->os_s = 0;
    que->os_e = 0;

    return 0;
}

IEL_QUE_API
iel_que_sz IEL_QUE_IDENT(size) (struct iel_que_st const *que) {
    iel_que_idx chunks = que->chunk_e - que->chunk_s;
    return (chunks << IEL_QUE_CHUNK_ENTS_BIT) + que->os_e - que->os_s;
}

IEL_QUE_API
int IEL_QUE_IDENT(push1) (struct iel_que_st *que, IEL_QUE_ELEM v) {
    if (!que->os_e) {
        if (que->chunk_e >= que->mapcap) {
            iel_que_sz mapcap_new = que->mapcap << 1;
            iel_que_gmap map_new = realloc(que->map, IEL_QUE_MAPSZ4(mapcap_new));
            if (!map_new)
                return -1;
            que->map = map_new;
            que->mapcap = mapcap_new;
        }
        IEL_QUE_ELEM *chunk_new = malloc(IEL_QUE_CHUNK_SZ);
        if (!chunk_new)
            return -1;
        IEL_QUE_MAPOF(que)[que->chunk_e] = chunk_new;
    }
    IEL_QUE_MAPOF(que)[que->chunk_e][que->os_e] = v;
    que->os_e = (que->os_e + 1) & IEL_QUE_CHUNK_ENTS_MASK;
    if (!que->os_e)
        que->chunk_e++;
    return 0;
}

IEL_QUE_API
int IEL_QUE_IDENT(pop1) (struct iel_que_st *que, IEL_QUE_ELEM *vout) {
    if (que->chunk_e == que->chunk_s && que->os_e == que->os_s)
        return -1;
    *vout = IEL_QUE_MAPOF(que)[que->chunk_s][que->os_s];
    que->os_s = (que->os_s + 1) & IEL_QUE_CHUNK_ENTS_MASK;
    if (!que->os_s) {
        free(IEL_QUE_MAPOF(que)[que->chunk_s]);
        que->chunk_s++;
    }
    if (que->chunk_e == que->chunk_s && que->os_e == que->os_s) {  // empty
        // reset/simple trim
        if (que->os_e) {  // chunk_e allocated
            free(IEL_QUE_MAPOF(que)[que->chunk_e]);
            que->os_s = que->os_e = 0;
        }
        que->chunk_s = que->chunk_e = 0;
    }
    return 0;
}

IEL_QUE_API
void IEL_QUE_IDENT(qtrim) (struct iel_que_st *que) {
    if (que->chunk_s > 4096 || que->chunk_s > (que->mapcap >> 3))
        IEL_QUE_IDENT(trim) (que);
}

IEL_QUE_API
void IEL_QUE_IDENT(trim) (struct iel_que_st *que) {
    iel_que_sz offs = que->chunk_s;
    iel_que_sz sz = que->chunk_e - que->chunk_s;
    iel_que_sz mapcap_new;
    if (que->os_e) {  // chunk_e allocated
        if (sz || que->os_e != que->os_s)  // one or more chunk allocated
            ++sz;
        else {  // free allocated empty chunk
            free(IEL_QUE_MAPOF(que)[que->chunk_e]);
            que->os_s = que->os_e = 0;
        }
    }
    memmove(que->map, &IEL_QUE_MAPOF(que)[que->chunk_s], IEL_QUE_MAPSZ4(sz));
    // memset(que->map + sz, 0, IEL_QUE_MAPSZ4(que->mapcap - sz));
    que->chunk_s = 0;
    que->chunk_e -= offs;
    mapcap_new = sz < IEL_QUE_MINCHUNKS ? IEL_QUE_MINCHUNKS : sz;
    iel_que_gmap map_new = realloc(que->map, IEL_QUE_MAPSZ4(mapcap_new));
    if (!map_new)
        return;
    que->map = map_new;
    que->mapcap = mapcap_new;
}

IEL_QUE_API
void IEL_QUE_IDENT(del) (struct iel_que_st *que) {
    for (iel_que_idx i = que->chunk_s; i != que->chunk_e; ++i)
        free(IEL_QUE_MAPOF(que)[i]);
    if (que->os_e)
        free(IEL_QUE_MAPOF(que)[que->chunk_e]);
    free(que->map);
}
#endif /* ifdef IEL_QUE_IMPL */

#undef IEL_QUE_TPL

#else /* ifdef IEL_QUE_TPL */
#error Do not include que.tpl.h directly
#endif /* ifdef IEL_QUE_TPL */
