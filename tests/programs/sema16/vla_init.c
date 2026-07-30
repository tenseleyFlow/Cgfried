// FLAGS: -fsyntax-only
// ERROR_EXPECTED: may not be initialized
void f(int n){ int a[n] = {1}; (void)a; }
