#ifndef IEL_CONFIG_H_
#define IEL_CONFIG_H_

#ifdef __cplusplus
#define IEL_DECL_C extern "C"
#define IEL_DECL_C_BEGIN extern "C" {
#define IEL_DECL_C_END }
#else
#define IEL_DECL_C
#define IEL_DECL_C_BEGIN
#define IEL_DECL_C_END
#endif /* ifdef __cplusplus */

#if (defined(__cplusplus) && __cplusplus >= 201103L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
/* Never mix compiler-specific attributes with C++11/C23 [[]] attributes */
#define IEL_C23ATTR_NAMESP(namesp,attr,alt) [[namesp :: attr]]
#else
#define IEL_C23ATTR_NAMESP(namesp,attr,alt) alt
#endif

#if (defined(__cplusplus) && __cplusplus >= 201703L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
#define IEL_C23ATTR_NODISCARD [[nodiscard]]
#else
#define IEL_C23ATTR_NODISCARD
#endif /* <has nodiscard> */

#if (defined(__cplusplus) && __cplusplus >= 201402L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
#define IEL_C23ATTR_DEPRECATED(MSG) [[deprecated MSG]]
#else
#define IEL_C23ATTR_DEPRECATED(MSG)
#endif /* <has deprecated> */

#if defined(__GNUC__)
#define IEL_ATTR_GNU(x) IEL_C23ATTR_NAMESP(gnu, x, __attribute__((x)))
#define IEL_ATTR_MSC(x)

#if !defined(__clang__) && __GNUC__ >= 11
/* MUST apply to the declaration AND the definition */
#define IEL_FNATTR_ALLOC(dealloc) IEL_ATTR_GNU(malloc dealloc)
#else
#define IEL_FNATTR_ALLOC(dealloc) IEL_ATTR_GNU(malloc)
#endif /* <gcc 11+> */

#define IEL_FNATTR_NODISCARD_ IEL_ATTR_GNU(warn_unused_result)

#ifdef __has_builtin
#define IEL_HAS_BUILTIN_GNU_(x) __has_builtin(x)
#else
#define IEL_HAS_BUILTIN_GNU_(x) 0
#endif

#ifdef __has_attribute
#define IEL_HAS_ATTRIBUTE_GNU_(x) __has_attribute(x)
#else
#define IEL_HAS_ATTRIBUTE_GNU_(x) 0
#endif

/* START internal only */

#if IEL_HAS_BUILTIN_GNU_(__builtin_assume)
#define IEL_STATTR_ASSUME(x) __builtin_assume(x)  /* clang >= 3.6 */
#elif IEL_HAS_ATTRIBUTE_GNU_(assume)
#define IEL_STATTR_ASSUME(x) IEL_ATTR_GNU(assume(x))  /* gcc >= 13 */
#else
#define IEL_STATTR_ASSUME(x) \
    do { if (!(x)) __builtin_unreachable(); } while (0)
#endif /* if IEL_HAS_BUILTIN_GNU_(__builtin_assume) */

/* Doesn't support old icc */
#if (defined __clang__ && IEL_HAS_BUILTIN_GNU_(__builtin_assume_aligned)) || \
    (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
#define IEL_ASSUME_ALIGNED(ptr,al) __builtin_assume_aligned(ptr,al)
#else
#define IEL_ASSUME_ALIGNED(ptr,al) ptr
#endif /* <has builtin> */

/* END internal only */

#undef IEL_HAS_BUILTIN_GNU_
#undef IEL_HAS_ATTRIBUTE_GNU_

#elif defined(_MSC_VER)

#include <sal.h>
#define IEL_ATTR_GNU(x)
#define IEL_ATTR_MSC(x) IEL_C23ATTR_NAMESP(msvc, x, __declspec(x))
#define IEL_FNATTR_ALLOC(dealloc) IEL_ATTR_MSC(restrict)
#define IEL_FNATTR_NODISCARD_ _Check_return_
/* TODO: build with /analyze */
/* TODO: more SAL, maybe? */

#define IEL_STATTR_ASSUME(x) __assume(x)
#define IEL_ASSUME_ALIGNED(ptr,al) ptr

#else
#define IEL_ATTR_GNU(x)
#define IEL_ATTR_MSC(x)
#define IEL_FNATTR_ALLOC(dealloc)
#define IEL_FNATTR_NODISCARD_

#define IEL_STATTR_ASSUME(x)
#define IEL_ASSUME_ALIGNED(ptr,al) ptr

#endif /* if defined(__GNUC__) || defined(__clang__) */

/* Functions with this attribute must NOT read or modify any global state */
#define IEL_FNATTR_CONST IEL_ATTR_GNU(const) IEL_ATTR_MSC(noalias)
/* Functions with this attribute must NOT modify any global state */
#define IEL_FNATTR_PURE IEL_ATTR_GNU(pure)
/* arg_idx starts from 1 */
#define IEL_FNATTR_ALLOCSZ(arg_idx) IEL_ATTR_GNU(alloc_size (arg_idx))
#define IEL_FNATTR_ALLOCAL(arg_idx) IEL_ATTR_GNU(alloc_align (arg_idx))

/* usage: IEL_ATTR_UNAVAIL_() or IEL_ATTR_UNAVAIL_(("message")) */
#define IEL_ATTR_UNAVAIL_(MSG) \
    IEL_C23ATTR_DEPRECATED(MSG) \
    IEL_ATTR_GNU(unavailable MSG) IEL_ATTR_MSC(deprecated MSG) \

#define IEL_SYMATTR_VISIBLE_ IEL_ATTR_GNU(visibility("default"))
#define IEL_SYMATTR_HIDDEN_ IEL_ATTR_GNU(visibility("hidden"))
#define IEL_SYMATTR_HIDDEN_CONSUME_ IEL_ATTR_UNAVAIL_(("Non-stable API used while stable requested; Linking may fail"))

/* different from C++ standard noexcept or throw(), UB if exception thrown,
 * use in public API declarations only.
 */
#define IEL_FNATTR_NOTHROW IEL_ATTR_GNU(nothrow) IEL_ATTR_MSC(nothrow)

/* MUST apply to both the declaration AND the definition */
#define IEL_FNATTR_NODISCARD IEL_C23ATTR_NODISCARD IEL_FNATTR_NODISCARD_


#ifdef IEL_BUILDING

#if IEL_BUILDING == 1 /* building shared */
#define IEL_STABLE_API_ IEL_SYMATTR_VISIBLE_ IEL_ATTR_MSC(dllexport)
#define IEL_API_ IEL_SYMATTR_HIDDEN_
#else /* building static */
#define IEL_STABLE_API_ IEL_SYMATTR_VISIBLE_
#define IEL_API_ IEL_SYMATTR_VISIBLE_
#endif /* IEL_BUILDING == 1 */

#else /* consuming */

#ifdef IEL_USE_STABLE

#if IEL_USE_STABLE == 1 /* shared */
#define IEL_STABLE_API_ IEL_ATTR_MSC(dllimport)
#define IEL_API_ IEL_SYMATTR_HIDDEN_CONSUME_
#else /* static, stable-compatible */
#define IEL_STABLE_API_
#define IEL_API_ IEL_SYMATTR_HIDDEN_CONSUME_
#endif

#else /* static */

#define IEL_STABLE_API_
#define IEL_API_

#endif /* ifdef IEL_USE_STABLE */

#endif /* ifdef IEL_BUILDING */


#define IEL_GBL IEL_DECL_C IEL_API_ extern
#define IEL_API IEL_DECL_C IEL_API_ IEL_FNATTR_NOTHROW

#define IEL_STABLE_GBL IEL_DECL_C IEL_STABLE_API_ extern
#define IEL_STABLE_API IEL_DECL_C IEL_STABLE_API_ IEL_FNATTR_NOTHROW

/* compulsory for internal globals, optional for functions
 * NEVER USE THE KEYWORD extern IN YOUR CODE!!!
 * NEVER USE THIS ATTRIBUTE IN PUBLIC HEADERS!!!
 */
#define IEL_INTERNAL_GBL IEL_SYMATTR_HIDDEN_ extern

#ifndef IEL_BUILDING
/* undef internal macros for consumers */
#undef IEL_INTERNAL_GBL
#undef IEL_STATTR_ASSUME
#undef IEL_ASSUME_ALIGNED
#endif

#endif /* ifndef IEL_CONFIG_H_ */
