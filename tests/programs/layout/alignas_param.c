// FLAGS: -fsyntax-only
// ERROR_EXPECTED: cannot appear on a function parameter
void f(_Alignas(16) int p);
