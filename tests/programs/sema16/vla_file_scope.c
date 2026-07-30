// FLAGS: -fsyntax-only
// ERROR_EXPECTED: variably modified type at file scope
int n; int a[n];
