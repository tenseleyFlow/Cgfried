#ifdef CGF_APPLE_SDK_VA_LIST_FIRST
#include <stdio.h>
#endif
#include <stdarg.h>

#ifndef _VA_LIST_T
#error "Cgfried's Apple stdarg must publish the SDK va_list guard"
#endif

#ifndef __SIZEOF_INT128__
#error "native GNU integer-128 support must remain advertised"
#endif

_Static_assert(sizeof(__uint128_t) == 16, "native TI width");
_Static_assert(_Alignof(__uint128_t) == 16, "native TI alignment");

unsigned long long macos_stdarg_native_ti(unsigned int count, ...)
{
    va_list args;
    va_start(args, count);
    unsigned long long value = va_arg(args, unsigned long long);
    va_end(args);

    __uint128_t wide = ((__uint128_t)value << 72) + 0x1234;
    wide = wide * 3 + value;
    return (unsigned long long)(wide >> 64) ^ (unsigned long long)wide;
}
