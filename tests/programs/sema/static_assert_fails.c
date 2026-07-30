// FLAGS: -fsyntax-only
// ERROR_EXPECTED: static assertion failed: this one is false
_Static_assert(1 == 2, "this one is false");
