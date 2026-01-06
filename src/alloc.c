#include <iel_priv/alloc.h>

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
#else
    return aligned_alloc(al, sz);
#endif
}

void iel_aligned_free(void *ptr) {
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
