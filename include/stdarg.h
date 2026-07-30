/* cgfried freestanding <stdarg.h> — preprocessing-complete now; the va_*
 * builtins become real compiler magic in Sprint 28 (any USE before then is
 * a front-end error, not a silent lie). */
#ifndef _CGF_STDARG_H
#define _CGF_STDARG_H

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

#endif
