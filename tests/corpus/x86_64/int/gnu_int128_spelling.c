// FLAGS: -std=c17
// OPT_EQ: all
// EXIT_CODE: 0
/* GNU's source spelling and the compiler-provided Clang/GCC typedef names
 * are aliases for the same signed and unsigned types that mode(TI) exposes.
 * Keep this in strict mode: every spelling begins with a reserved `__` name
 * and both GCC and Clang accept them without a pedantic diagnostic. */
typedef int mode_i128 __attribute__((mode(TI)));
typedef unsigned int mode_u128 __attribute__((mode(TI)));
typedef signed __int128 i128;
typedef unsigned __int128 u128;

_Static_assert(__builtin_types_compatible_p(i128, __int128_t),
               "signed builtin typedef identity");
_Static_assert(__builtin_types_compatible_p(u128, __uint128_t),
               "unsigned builtin typedef identity");
_Static_assert(__builtin_types_compatible_p(i128, mode_i128),
               "signed mode identity");
_Static_assert(__builtin_types_compatible_p(u128, mode_u128),
               "unsigned mode identity");
_Static_assert(sizeof(__int128) == 16 && _Alignof(__int128) == 16,
               "signed layout");
_Static_assert(sizeof(__uint128_t) == 16 && _Alignof(__uint128_t) == 16,
               "unsigned layout");

/* This is the C-facing shape in Apple's mach/arm/_structs.h.  Native
 * __uint128_t means the SDK no longer needs the benchmark-only aggregate
 * substitution that could prove only size, not integer identity. */
struct apple_neon_shape {
    __uint128_t q[32];
    unsigned int fpsr;
    unsigned int fpcr;
};

_Static_assert(_Alignof(struct apple_neon_shape) == 16,
               "Apple NEON state alignment");
_Static_assert(sizeof(struct apple_neon_shape) == 528, "Apple NEON state size");

#define UONE ((__uint128_t)1)
#define UBIT(N) (UONE << (N))

static __uint128_t folded = UBIT(100) + UBIT(65) + 17;

static __int128_t signed_round_trip(__int128 value)
{
    return value * 3 - 2;
}

int main(void)
{
    __uint128_t value = folded;
    __int128 signed_value = signed_round_trip(-((__int128)UBIT(90)));

    if ((unsigned long long)value != 17)
        return 1;
    if ((unsigned long long)(value >> 64) != ((1ull << 36) + 2))
        return 2;
    if (signed_value != -((__int128)UBIT(90)) * 3 - 2)
        return 3;
    return 0;
}
