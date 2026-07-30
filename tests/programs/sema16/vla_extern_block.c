// FLAGS: -fsyntax-only
// ERROR_EXPECTED: automatic storage
void f(int n){ extern int a[n]; (void)a; }
