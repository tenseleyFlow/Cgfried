// FLAGS: -fsyntax-only
// ERROR_EXPECTED: '_Atomic'-qualified function type
_Atomic(int(void)) a;
