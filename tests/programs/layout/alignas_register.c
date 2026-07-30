// FLAGS: -fsyntax-only
// ERROR_EXPECTED: cannot appear on a 'register' declaration
void f(void) { _Alignas(16) register int x; (void)x; }
