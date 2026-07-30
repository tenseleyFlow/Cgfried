// FLAGS: -fsyntax-only
// ERROR_EXPECTED: variably modified type at file scope
// An assignment bound makes this a VLA; file scope forbids VM types.
int x; int a[x=2];
