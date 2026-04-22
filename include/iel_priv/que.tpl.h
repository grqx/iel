#ifdef IEL_QUE_TPL
// #define IEL_QUE_TPL (/*ChunkEntsBit=*/, /*MinChunks=*/, /*type=*/, /*pfx=*/, /*sfx=*/, /*api=*/, /*align=*/,)

#include <assert.h>
#include <iel/config.h>
#include <iel_priv/pp.h>
IEL_PP_CHECK_ARGC(7, IEL_QUE_TPL)
#define IEL_QUE_GETARG(n) IEL_PP_GETARG(n, IEL_QUE_TPL)

#define IEL_QUE_CHUNK_ENTS_BIT IEL_QUE_GETARG(0)
#define IEL_QUE_MINCHUNKS IEL_QUE_GETARG(1)
#define IEL_QUE_ELEM IEL_QUE_GETARG(2)
#define IEL_QUE_IDENT(x) IEL_PP_CONCAT3(IEL_QUE_GETARG(3), x, IEL_QUE_GETARG(4))
#define IEL_QUE_API IEL_QUE_GETARG(5)
#define IEL_QUE_ALIGN IEL_QUE_GETARG(6)

static_assert(
    IEL_QUE_CHUNK_ENTS_BIT <= 32,
    "Too many elements in a chunk! Reduce ChunkEntsBit!");
static_assert(
    IEL_QUE_ALIGN <= sizeof(IEL_QUE_ELEM) &&
    sizeof(IEL_QUE_ELEM) % IEL_QUE_ALIGN == 0,
    "Invalid alignment requested!");
#include <iel/quedecl.h>

/* Precondition: que */
IEL_QUE_API IEL_FNATTR_NODISCARD
int IEL_QUE_IDENT(init) (struct iel_que_st *que, iel_que_sz initcap_map);

/* Precondition: que, and que is initialised correctly */
IEL_QUE_API IEL_FNATTR_NODISCARD
iel_que_sz IEL_QUE_IDENT(size) (struct iel_que_st const *que);

/* Precondition: que, and que is initialised correctly
 * Reserve space for one new element at the front of que.
 * The return value points to an uninitialised element, and
 * remains valid only until the next non-const operation on que.
 */
IEL_QUE_API IEL_FNATTR_NODISCARD IEL_ATTR_GNU(assume_aligned(IEL_QUE_ALIGN))
IEL_QUE_ELEM *IEL_QUE_IDENT(rsv1) (struct iel_que_st *que);

/* Precondition: que, and que is initialised correctly */
IEL_QUE_API IEL_FNATTR_NODISCARD
int IEL_QUE_IDENT(pop1) (struct iel_que_st *que, IEL_QUE_ELEM *vout, iel_que_idx chunk_stop, iel_que_offset os_stop);

/* Precondition: que && blk_out, and que is initialised correctly */
IEL_QUE_API
size_t IEL_QUE_IDENT(pop_to) (struct iel_que_st *que, IEL_QUE_ELEM *blk_out, size_t blk_sz);

/* Precondition: que, and que is initialised correctly */
IEL_QUE_API
void IEL_QUE_IDENT(del) (struct iel_que_st *que);

/* Precondition: que, and que is initialised correctly */
IEL_QUE_API
void IEL_QUE_IDENT(qtrim) (struct iel_que_st *que);

/* Precondition: que, and que is initialised correctly */
IEL_QUE_API
void IEL_QUE_IDENT(trim) (struct iel_que_st *que);

#ifdef IEL_QUE_IMPL
#include <string.h>
#include <stdlib.h>
#include <iel_priv/alloc.h>

#define IEL_QUE_CHUNK_ENTS_MASK ((1ULL << IEL_QUE_CHUNK_ENTS_BIT) - 1)
#define IEL_QUE_CHUNK_SZ (sizeof(IEL_QUE_ELEM) << IEL_QUE_CHUNK_ENTS_BIT)
#define IEL_QUE_MAPOF(que) ((IEL_QUE_ELEM **) que->map)
#define IEL_QUE_MAPSZ4(cap) (cap * sizeof(IEL_QUE_ELEM *))

IEL_QUE_API IEL_FNATTR_NODISCARD
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

IEL_QUE_API IEL_FNATTR_NODISCARD
iel_que_sz IEL_QUE_IDENT(size) (struct iel_que_st const *que) {
    iel_que_idx chunks = que->chunk_e - que->chunk_s;
    return (chunks << IEL_QUE_CHUNK_ENTS_BIT) + que->os_e - que->os_s;
}

IEL_QUE_API IEL_FNATTR_NODISCARD
IEL_QUE_ELEM *IEL_QUE_IDENT(rsv1) (struct iel_que_st *que) {
    IEL_QUE_ELEM *retv;
    if (!que->os_e) {
        if (que->chunk_e >= que->mapcap) {
            iel_que_sz mapcap_new = que->mapcap << 1;
            iel_que_gmap map_new = realloc(que->map, IEL_QUE_MAPSZ4(mapcap_new));
            if (!map_new)
                return NULL;
            que->map = map_new;
            que->mapcap = mapcap_new;
        }
        IEL_QUE_ELEM *chunk_new = (IEL_QUE_ELEM *)iel_aligned_alloc(IEL_QUE_ALIGN, IEL_QUE_CHUNK_SZ);
        if (!chunk_new)
            return NULL;
        IEL_QUE_MAPOF(que)[que->chunk_e] = chunk_new;
    }
    retv = &IEL_QUE_MAPOF(que)[que->chunk_e][que->os_e];
    que->os_e = (que->os_e + 1) & IEL_QUE_CHUNK_ENTS_MASK;
    if (!que->os_e)
        que->chunk_e++;
    return retv;
}

IEL_QUE_API IEL_FNATTR_NODISCARD
int IEL_QUE_IDENT(pop1) (struct iel_que_st *que, IEL_QUE_ELEM *vout, iel_que_idx chunk_stop, iel_que_offset os_stop) {
    if (que->chunk_e == que->chunk_s && que->os_e == que->os_s)  // empty if all zero
        return -1;
    *vout = IEL_QUE_MAPOF(que)[que->chunk_s][que->os_s];
    que->os_s = (que->os_s + 1) & IEL_QUE_CHUNK_ENTS_MASK;
    if IEL_UNLIKELY (!que->os_s) {  // free old chunk
        iel_aligned_free(IEL_QUE_MAPOF(que)[que->chunk_s]);
        que->chunk_s++;
    }
    if IEL_UNLIKELY (que->chunk_e == que->chunk_s && que->os_e == que->os_s) {  // last
        // reset/simple trim
        if (que->os_e)  // chunk_e allocated
            iel_aligned_free(IEL_QUE_MAPOF(que)[que->chunk_e]);
        que->chunk_s = que->chunk_e = 0;
        que->os_s = que->os_e = 0;
        return 1;
    }
    if IEL_UNLIKELY (chunk_stop == que->chunk_s && os_stop == que->os_s)  // last
        return 1;
    return 0;
}

IEL_QUE_API
size_t IEL_QUE_IDENT(pop_to) (struct iel_que_st *que, IEL_QUE_ELEM *blk_out, size_t blk_sz) {
    size_t sz = 0;

    if IEL_UNLIKELY (!blk_sz) return 0;
    while (1) {
        size_t span;
        if (que->chunk_e == que->chunk_s) {  // last chunk
            span = que->os_e - que->os_s;

            // TODO: unsafe variant where this branch is removed?
            if (!span) {  // empty, trim and return
out_que_empty:;
                if (que->os_e)  // chunk_e allocated
                    iel_aligned_free(IEL_QUE_MAPOF(que)[que->chunk_e]);
                que->chunk_s = que->chunk_e = 0;
                que->os_s = que->os_e = 0;
                return sz;
            }
        } else {  // complete chunk
            span = (1ULL << IEL_QUE_CHUNK_ENTS_BIT) - que->os_s;
        }

        IEL_QUE_ELEM const * const restrict src = (IEL_QUE_ELEM const *)IEL_ASSUME_ALIGNED(
            &IEL_QUE_MAPOF(que)[que->chunk_s][que->os_s], IEL_QUE_ALIGN);
        if (span >= blk_sz) {  // block exhausted, rare
            memcpy(blk_out, src, blk_sz * sizeof(IEL_QUE_ELEM));
            sz += blk_sz;

            que->os_s = (que->os_s + span) & IEL_QUE_CHUNK_ENTS_MASK;
            if (!que->os_s) {  // chunk also exhausted, rare (span == blk_sz)
                iel_aligned_free(IEL_QUE_MAPOF(que)[que->chunk_s]);
                que->chunk_s++;
            }
            return sz;
        }

        memcpy(blk_out, src, span * sizeof(IEL_QUE_ELEM));
        sz += span;
        blk_out += span;
        blk_sz -= span;

        que->os_s = (que->os_s + span) & IEL_QUE_CHUNK_ENTS_MASK;
        if (que->os_s) goto out_que_empty;  // chunk not exhausted, rare (last chunk)
        iel_aligned_free(IEL_QUE_MAPOF(que)[que->chunk_s]);
        que->chunk_s++;
    }
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
        if (sz || que->os_e != que->os_s)  // one or more chunks allocated
            ++sz;
        else {  // free allocated empty chunk
            iel_aligned_free(IEL_QUE_MAPOF(que)[que->chunk_e]);
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
        iel_aligned_free(IEL_QUE_MAPOF(que)[i]);
    if (que->os_e)
        iel_aligned_free(IEL_QUE_MAPOF(que)[que->chunk_e]);
    free(que->map);
}
#endif /* ifdef IEL_QUE_IMPL */

#undef IEL_QUE_TPL

#else /* ifdef IEL_QUE_TPL */
#error Do not include que.tpl.h directly
#endif /* ifdef IEL_QUE_TPL */
