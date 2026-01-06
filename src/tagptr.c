#include <iel/tagptr.h>

#ifdef UINTPTR_MAX

IEL_FNATTR_NODISCARD
void *iel_tagptr_untag(void const *ptr, size_t al, uintmax_t *tagout) {
    *tagout = (uintptr_t)ptr & (al - 1);
    return (void *)((uintptr_t)ptr & ~(uintptr_t)(al - 1));
}

IEL_FNATTR_NODISCARD
void *iel_tagptr_tag(void const *ptr, size_t minal, uintmax_t tag) {
    if (tag >= minal)
        return NULL;
    return (void *)((uintptr_t)ptr | tag);
}

#else
#error uintptr_t required for tagged pointers
#endif
