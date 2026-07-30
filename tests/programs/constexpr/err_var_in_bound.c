// FLAGS: -fsyntax-only
// ERROR_EXPECTED: variably modified type at file scope
// The classic missing-constant case: at file scope the diagnosis is the
// VM-type error, matching gcc.
int n; int a[n];
