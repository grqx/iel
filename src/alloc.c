#if IEL_HAVE_POSIX2001_MEMALIGN
#define _POSIX_C_SOURCE 200112L
#endif

#include <iel_priv/alloc.h>
#include <iel_priv/generated/feat.h>

#ifdef _MSC_VER
#include <malloc.h>
#else
#include <stdlib.h>
#endif

IEL_FNATTR_NODISCARD
IEL_FNATTR_ALLOC((iel_aligned_free))
void *iel_aligned_alloc(size_t al, size_t sz) {
#ifdef _MSC_VER
    return _aligned_malloc(sz, al);
#elif IEL_HAVE_ALIGNED_ALLOC
    return aligned_alloc(al, sz);
#elif IEL_HAVE_POSIX2001_MEMALIGN
    void *p;
    if (posix_memalign(&p, al, sz))
        return NULL;
    return p;
#else
#error aligned allocator unavailable
#endif
}

void iel_aligned_free(void *ptr) {
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
