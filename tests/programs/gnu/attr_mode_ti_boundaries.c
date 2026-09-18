// FLAGS: -std=gnu17 -fsyntax-only -fmax-errors=0
// ERROR_EXPECTED: conversion between mode(TI) and floating types is not yet supported
// ERROR_EXPECTED: atomic mode(TI) objects are not yet supported
// ERROR_EXPECTED: mode(TI) bit-fields are not yet supported
// ERROR_EXPECTED: mode(TI) switch controlling expressions are not yet supported
// ERROR_EXPECTED: static initialization of a mode(TI) object is not yet supported
// ERROR_EXPECTED: 128-bit mode(TI) arithmetic is not yet supported in constant expressions
// ERROR_EXPECTED: mode(TI) enumerated types are not yet supported
// ERROR_EXPECTED: reverse scalar storage order for mode(TI) member
// ERROR_EXPECTED: mode(TI) operands to checked-overflow builtins are not yet supported
// ERROR_EXPECTED: compound assignment between an atomic object and a mode(TI) operand
// ERROR_EXPECTED: floating comparison builtin conversion from mode(TI) is not yet supported
/* Every accepted mode(TI) operation has real two-limb lowering. These are
 * the remaining boundaries where a one-limb constant image, scalar atomic
 * IR operation, or storage-order transform would otherwise silently produce
 * the wrong program. Keep them named until each separate facility lands. */
typedef unsigned int u128 __attribute__((mode(TI)));

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

static u128 initialized = 1;

_Static_assert(((u128)1 << 64) != 0, "wide constant expression");

enum WideEnum { WIDE_ZERO } __attribute__((mode(TI)));

struct __attribute__((scalar_storage_order("big-endian"))) ReverseWide {
    u128 value[2];
};

int checked_overflow(u128 value)
{
    unsigned long result;

    return __builtin_add_overflow(value, 1ul, &result);
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
