// FLAGS: -std=gnu17 -fsyntax-only -fmax-errors=0
// ERROR_EXPECTED: atomic mode(TI) objects are not yet supported
// ERROR_EXPECTED: the cast to 'unsigned mode(TI) integer' is not pointer-width
// ERROR_EXPECTED: mode(TI) enumerated types are not yet supported
// ERROR_EXPECTED: reverse scalar storage order for mode(TI) member
// ERROR_EXPECTED: mode(TI) operands to checked-overflow multiplication
// ERROR_EXPECTED: compound assignment between an atomic object
// ERROR_EXPECTED: overflow in constant expression
// ERROR_EXPECTED: division by zero in a constant expression
// ERROR_EXPECTED: shift count is out of range for a 128-bit type
/* Every accepted mode(TI) operation has real two-limb lowering. These are
 * the remaining boundaries where scalar atomic IR operation or storage-order
 * transform would otherwise silently produce the wrong program. Floating
 * conversions and comparisons, TI bit-fields, and switch controls are
 * implemented; keep the remaining boundaries named until each separate
 * facility lands. */
typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

#define UONE ((u128)1)
#define UBIT(N) (UONE << (N))
#define IMAX ((i128)(UBIT(127) - UONE))

static int anchor;
static u128 address = (u128)&anchor;

double float_conversion(u128 value)
{
    return (double)value;
}

_Atomic(u128) atomic_value;

struct BitField {
    u128 value : 1;
};

int switch_control(u128 value)
{
    switch (value) {
    default:
        return 0;
    }
}

_Static_assert(((u128)1 << 64) != 0, "wide constant expression");
_Static_assert(IMAX + 1, "signed wide overflow must fail");
_Static_assert(UBIT(100) / 0, "wide division by zero must fail");
_Static_assert(UONE << 128, "wide shift count must fail");

enum WideEnum { WIDE_ZERO } __attribute__((mode(TI)));

struct __attribute__((scalar_storage_order("big-endian"))) ReverseWide {
    u128 value[2];
};

int checked_overflow_multiply(u128 value)
{
    unsigned long result;

    return __builtin_mul_overflow(value, 1ul, &result);
}

_Atomic(unsigned long) atomic_word;

void atomic_compound(u128 value)
{
    atomic_word += value;
}

int builtin_float_compare(u128 value)
{
    return __builtin_isless(value, 1.0);
}
