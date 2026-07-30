// FLAGS: -fsyntax-only
// ERROR_EXPECTED: cannot be variably modified
void f(int n){ _Atomic int a[n]; (void)a; }
