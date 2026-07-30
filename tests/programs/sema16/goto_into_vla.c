// FLAGS: -fsyntax-only
// ERROR_EXPECTED: jump into scope of identifier 'a' with variably modified type
void f(int n){ goto in; { int a[n]; in: a[0]=1; } }
