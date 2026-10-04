#ifndef _VA_LIST_T
#error "the Apple SDK va_list redeclaration must be suppressed"
#endif

#ifndef __SIZEOF_INT128__
#error "native GNU integer-128 support must remain advertised"
#endif

/* This is the incompatible Apple SDK fallback when _VA_LIST_T is absent. */
#ifndef _VA_LIST_T
typedef void *va_list;
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
