// FLAGS: -fsyntax-only
// ERROR_EXPECTED: 'x' is not a constant expression
int x; int a[x++];
