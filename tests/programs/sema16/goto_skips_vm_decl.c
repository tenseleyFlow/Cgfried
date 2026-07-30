// FLAGS: -fsyntax-only
// ERROR_EXPECTED: jump into scope
void f(int n){ goto skip; int a[n]; skip: (void)a; }
