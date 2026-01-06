#ifndef IEL_PRIV_ALLOC_H_
#define IEL_PRIV_ALLOC_H_

#include <iel/config.h>

#include <stddef.h>

void iel_aligned_free(void *ptr);

IEL_FNATTR_NODISCARD
IEL_FNATTR_ALLOCAL(1)
IEL_FNATTR_ALLOCSZ(2)
IEL_FNATTR_ALLOC((iel_aligned_free))
void *iel_aligned_alloc(size_t al, size_t sz);

#endif /* ifndef IEL_PRIV_ALLOC_H_ */
