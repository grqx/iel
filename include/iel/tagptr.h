#ifndef IEL_TAGPTR_H_
#define IEL_TAGPTR_H_

#include <iel/config.h>

#include <stddef.h>
#include <stdint.h>

/* MUST be called on the same thread before any other iel_tp APIs.
 * Could be called multiple times */
IEL_STABLE_API
void iel_tp_init(void);

struct iel_tp_untag_st {
    void *ptr;
    uintmax_t tag;
};

/* Precondition: al && !(al & (al - 1)), and
 * iel_tp_init() has been called before.
 * The return value is always positive so 0 is never returned.
 * If tagging is unsupported the return value would be 1.
 * the maximum tag is return value - 1 given alignment al.
 * This implies tagging UINTMAX_MAX is not possible through this API.
 */
IEL_STABLE_API IEL_FNATTR_NODISCARD IEL_FNATTR_PURE
uintmax_t iel_tp_max(size_t al);


/* Precondition: ptr && al && !(al & (al - 1)), and
 * ptr must be either:
 * - a pointer to an object aligned to al (XXX: unsupported, do we want this behavior?), or
 * - a tagged pointer produced by iel_tp_tag
 *    with the same al parameter, and
 * iel_tp_init() has been called before.
 * The returned ptr will be untagged and is guaranteed to be non-NULL.
 * Therefore ptr is safe to cast to an object pointer and access/dereference.
 * Accessing the value of tag invokes undefined behavior if
 * ptr is not a pointer returned by iel_tp_tag(), otherwise,
 * tag contains the tag paremeter passed to iel_tp_tag(), and
 * is guaranteed to be less than iel_tp_max(al).
 */
IEL_STABLE_API IEL_FNATTR_NODISCARD IEL_FNATTR_PURE
/* TODO: returns-nonnull */
struct iel_tp_untag_st iel_tp_untag(void const *ptr, size_t al);

/* Precondition: ptr && al && !(al & (al - 1)) && tag < iel_tp_max(al), and
 * the object at ptr must be aligned to al,
 * i.e. ptr must not be a tagged pointer, and
 * iel_tp_init() has been called before.
 * The return value will not be NULL.
 * Do not cast the return value to an object pointer
 * for storage, as the encoded data might not persist.
 */
IEL_STABLE_API IEL_FNATTR_NODISCARD IEL_FNATTR_PURE
/* TODO: returns-nonnull */
void *iel_tp_tag(void const *ptr, size_t al, uintmax_t tag);

#endif /* ifndef IEL_TAGPTR_H_ */
