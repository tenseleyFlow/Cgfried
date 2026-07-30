// FLAGS: -fsyntax-only
// ERROR_EXPECTED: automatic storage duration
void f(void) { int local; static int *p = &local; (void)p; }
