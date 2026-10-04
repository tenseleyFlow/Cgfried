#ifndef __SIZEOF_INT128__
#error "native GNU integer-128 support must be advertised"
#endif

#define UONE ((__uint128_t)1)

_Static_assert(__SIZEOF_INT128__ == 16, "TI width predefine");
_Static_assert(sizeof(__uint128_t) == 16, "unsigned TI width");
_Static_assert(_Alignof(__uint128_t) == 16, "unsigned TI alignment");
_Static_assert((UONE << 100) + (UONE << 65) + 17 > (UONE << 100),
               "unsigned TI constant arithmetic");
_Static_assert(((UONE << 100) >> 64) == (UONE << 36),
               "unsigned TI cross-limb shift");

unsigned long long native_integer_semantics(unsigned long long value)
{
    __uint128_t wide = ((__uint128_t)value << 80) + 0x1234;
    wide = wide * 3 + value;
    return (unsigned long long)(wide >> 64) ^ (unsigned long long)wide;
}
