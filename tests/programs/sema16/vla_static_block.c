// FLAGS: -fsyntax-only
// ERROR_EXPECTED: automatic storage
void f(int n){ static int a[n]; (void)a; }
