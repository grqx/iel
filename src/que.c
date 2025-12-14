#include <stdlib.h>

#include <iel/que.h>
#include <string.h>

// TODO: 9
#define IEL_PQ_CHUNK_ENTS_BIT 2
#define IEL_PQ_CHUNK_ENTS (1U << IEL_PQ_CHUNK_ENTS_BIT)
#define IEL_PQ_CHUNK_ENTS_MASK (IEL_PQ_CHUNK_ENTS - 1)
#define IEL_PQ_CHUNK_SZ (sizeof(iel_que_elem) << IEL_PQ_CHUNK_ENTS_BIT)
#define IEL_PQ_MINCHUNKS 16

#define PEXPR(x) static char (*__kaboom ## __COUNTER__)[x] = 1;
// PEXPR(sizeof(struct iel_que_st))
// PEXPR(IEL_PQ_CHUNK_SZ)

int iel_que_init(struct iel_que_st *que, iel_que_sz initcap_map) {
    if (initcap_map < IEL_PQ_MINCHUNKS)
        initcap_map = IEL_PQ_MINCHUNKS;
    que->map = malloc(initcap_map * sizeof(iel_que_elem_p));
    if (!que->map)
        return -1;

    que->mapcap = initcap_map;

    que->chunk_s = 0;
    que->chunk_e = 0;
    que->os_s = 0;
    que->os_e = 0;

    return 0;
}

iel_que_sz iel_que_size(struct iel_que_st *que) {
    iel_que_idx chunks = que->chunk_e - que->chunk_s;
    return (chunks << IEL_PQ_CHUNK_ENTS_BIT) + que->os_e - que->os_s;
}

int iel_que_push1(struct iel_que_st *que, iel_que_elem v) {
    if (!que->os_e) {
        if (que->chunk_e >= que->mapcap) {
            iel_que_sz mapcap_new = que->mapcap << 1;
            iel_que_map map_new = realloc(que->map, mapcap_new * sizeof(iel_que_elem_p));
            if (!map_new)
                return -1;
            que->map = map_new;
            que->mapcap = mapcap_new;
        }
        iel_que_elem_p chunk_new = malloc(IEL_PQ_CHUNK_SZ);
        if (!chunk_new)
            return -1;
        que->map[que->chunk_e] = chunk_new;
    }
    que->map[que->chunk_e][que->os_e] = v;
    que->os_e = (que->os_e + 1) & IEL_PQ_CHUNK_ENTS_MASK;
    if (!que->os_e)
        que->chunk_e++;
    return 0;
}

int iel_que_pop1(struct iel_que_st *que, iel_que_elem *vout) {
    if (que->chunk_e == que->chunk_s && que->os_e == que->os_s)
        return -1;
    *vout = que->map[que->chunk_s][que->os_s];
    que->os_s = (que->os_s + 1) & IEL_PQ_CHUNK_ENTS_MASK;
    if (!que->os_s) {
        free(que->map[que->chunk_s]);
        que->chunk_s++;
    }
    if (que->chunk_e == que->chunk_s && que->os_e == que->os_s) {  // empty
        // reset/simple trim
        if (que->os_e) {  // chunk_e allocated
            free(que->map[que->chunk_e]);
            que->os_s = que->os_e = 0;
        }
        que->chunk_s = que->chunk_e = 0;
    }
    return 0;
}

void iel_que_qtrim(struct iel_que_st *que) {
    if (que->chunk_s > 4096 || que->chunk_s > (que->mapcap >> 3))
        iel_que_trim(que);
}

void iel_que_trim(struct iel_que_st *que) {
    iel_que_sz offs = que->chunk_s;
    iel_que_sz sz = que->chunk_e - que->chunk_s;
    iel_que_sz mapcap_new;
    if (que->os_e) {  // chunk_e allocated
        if (sz)  // more than one chunk allocated
            ++sz;
        else {  // empty/single chunk
            if (que->os_e == que->os_s) {  // empty
                free(que->map[que->chunk_e]);
                que->os_s = que->os_e = 0;
            }
        }
    }
    memmove(que->map, &que->map[que->chunk_s], sz * sizeof(iel_que_elem_p));
    que->chunk_s = 0;
    que->chunk_e -= offs;
    mapcap_new = sz < IEL_PQ_MINCHUNKS ? IEL_PQ_MINCHUNKS : sz;
    iel_que_map map_new = realloc(que->map, mapcap_new * sizeof(iel_que_elem_p));
    if (!map_new)
        return;
    que->map = map_new;
    que->mapcap = mapcap_new;
}

void iel_que_del(struct iel_que_st *que) {
    for (iel_que_idx i = que->chunk_s; i != que->chunk_e; ++i)
        free(que->map[i]);
    if (que->os_e)
        free(que->map[que->chunk_e]);
    free(que->map);
}
