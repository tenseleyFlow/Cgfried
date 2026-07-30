// FLAGS: -fsyntax-only
// ERROR_EXPECTED: only allowed in a function prototype
void f(void){ int a[*]; (void)a; }
