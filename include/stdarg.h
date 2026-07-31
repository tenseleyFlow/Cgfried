/* cgfried freestanding <stdarg.h> (C17 7.16).
 *
 * Implements gcc's `__need___va_list` protocol, which glibc's own
 * <stdio.h> depends on: it does
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
 * The header is therefore RE-ENTRANT: it may be included once for the
 * partial form and again for the full one, so the two guards are
 * separate and `__need___va_list` is consumed on the way through. */

#if defined(__need___va_list)
#undef __need___va_list
#ifndef __CGF_GNUC_VA_LIST
#define __CGF_GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif

#else /* the full header */

#ifndef _CGF_STDARG_H
#define _CGF_STDARG_H

#ifndef __CGF_GNUC_VA_LIST
#define __CGF_GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

#endif /* _CGF_STDARG_H */
#endif /* __need___va_list */
