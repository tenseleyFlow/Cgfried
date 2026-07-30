// FLAGS: -fsyntax-only
// ERROR_EXPECTED: jump into scope
void f(int n){ { int a[n]; back: a[0]=1; } goto back; }
