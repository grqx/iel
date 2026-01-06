#ifndef IEL_TAGPTR_H_
#define IEL_TAGPTR_H_

#include <iel/config.h>

#include <stddef.h>
#include <stdint.h>

/* Precondition: ptr && al && tagout && !(al & (al - 1)), and
 * ptr must be either:
 * - a pointer to an object aligned to al, or
 * - a tagged pointer produced by iel_tagptr_tag
 *    with parameter minal <= al
 * The untagged pointer will be returned, and *tagout will contain the
 * tag, or 0 if the pointer is not tagged.
 */
IEL_STABLE_API IEL_FNATTR_NODISCARD
void *iel_tagptr_untag(void const *ptr, size_t al, uintmax_t *tagout);

/* Assumes: ptr && al && && !(al & (al - 1)), and
 * the object at ptr must be aligned to al.
 * A return value of NULL denotes failure.
 * Do not cast the return value to an object pointer
 * for storage, as the encoded data might not persist.
 */
IEL_STABLE_API IEL_FNATTR_NODISCARD
void *iel_tagptr_tag(void const *ptr, size_t minal, uintmax_t tag);

#endif /* ifndef IEL_TAGPTR_H_ */
