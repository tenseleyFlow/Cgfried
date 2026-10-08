/* cgfried freestanding <stdarg.h> (C17 7.16).
 *
 * Implements gcc's `__need___va_list` protocol, which glibc's own
 * <stdio.h> depends on, and Clang's `__need_va_list` protocol, which Apple
 * SDK headers use when they own the public va_list guard.
 *
 *     #define __need___va_list
 *     #include <stdarg.h>
 *
 * expecting ONLY the `__gnuc_va_list` typedef and none of the va_*
 * macros (its vprintf-family prototypes are written in terms of that
 * name). Without this arm, `#include <stdio.h>` fails on a
 * Debian/Ubuntu glibc with "'__gnuc_va_list' undeclared" — found by the
 * Sprint 28 header lane, in the container where a hosted compile
 * actually runs.
 *
 * The header is therefore RE-ENTRANT: it may be included once for either
 * partial form and again for the full one, so the guards are separate and
 * each `__need_*` request is consumed on the way through. */

#if defined(__need___va_list) || defined(__need_va_list)

#ifdef __need___va_list
#undef __need___va_list
#ifndef __CGF_GNUC_VA_LIST
#define __CGF_GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif
#endif

#ifdef __need_va_list
#undef __need_va_list
#ifndef __CGF_VA_LIST
#define __CGF_VA_LIST
typedef __builtin_va_list va_list;
#endif
#if defined(__APPLE__) && !defined(_VA_LIST_T)
#define _VA_LIST_T 1
#endif
#endif

#else /* the full header */

#ifndef _CGF_STDARG_H
#define _CGF_STDARG_H

#ifndef __CGF_GNUC_VA_LIST
#define __CGF_GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif

#ifndef __CGF_VA_LIST
#define __CGF_VA_LIST
#if !defined(__APPLE__) || !defined(_VA_LIST_T)
typedef __builtin_va_list va_list;
#endif
#endif
/* Apple's sys/_types/_va_list.h uses this public guard to arbitrate typedef
 * ownership. Publish it when Cgfried arrives first; when the SDK arrived
 * first, retain its ABI-equivalent void-pointer fallback instead of issuing a
 * conflicting second typedef. Sema accepts that documented Apple fallback as
 * a cursor on this target only. */
#if defined(__APPLE__) && !defined(_VA_LIST_T)
#define _VA_LIST_T 1
#endif
#if defined(__CGFRIED__) && defined(__GNUC__)
#define va_start(ap, ...) __builtin_va_start(ap, ##__VA_ARGS__)
#else
#define va_start(ap, last) __builtin_va_start(ap, last)
#endif
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

#endif /* _CGF_STDARG_H */
#endif /* __need___va_list */
