// FLAGS: -fsyntax-only
// ERROR_EXPECTED: '_Atomic'-qualified array type
_Atomic(int[3]) a;
