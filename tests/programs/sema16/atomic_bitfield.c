// FLAGS: -std=gnu17 -fsyntax-only -fmax-errors=0
// ERROR_EXPECTED: bit-field 'narrow' has atomic type
// ERROR_EXPECTED: bit-field 'wide' has atomic type

typedef unsigned __int128 u128;

struct InvalidAtomicBitFields {
    _Atomic(int) narrow : 1;
    _Atomic(u128) wide : 1;
};
