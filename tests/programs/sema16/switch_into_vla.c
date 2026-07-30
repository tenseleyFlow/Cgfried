// FLAGS: -fsyntax-only
// ERROR_EXPECTED: switch jumps into scope
void f(int n){ switch(n){ case 0: { int a[n]; case 1: a[0]=1; } } }
