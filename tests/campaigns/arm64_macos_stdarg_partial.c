#define __need_va_list
#include <stdarg.h>

#ifndef _VA_LIST_T
#error "the Apple partial stdarg form must publish the SDK va_list guard"
#endif

#ifdef va_start
#error "the partial stdarg form must not expose va_start"
#endif

#ifdef __need_va_list
#error "the partial stdarg request must be consumed"
#endif

int macos_stdarg_partial(va_list args)
{
    return args != (va_list)0;
}

/* A partial request must not poison the later full include. */
#include <stdarg.h>

#ifndef va_start
#error "the full include after a partial request must expose va_start"
#endif

int macos_stdarg_partial_then_full(unsigned int count, ...)
{
    va_list args;
    va_start(args, count);
    int value = va_arg(args, int);
    va_end(args);
    return value;
}
