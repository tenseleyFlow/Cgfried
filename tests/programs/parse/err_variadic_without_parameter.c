// FLAGS: -std=c17 -pedantic-errors -fsyntax-only
// ERROR_EXPECTED: ISO C requires a parameter declaration before '...'
// FE-H-01: the ellipsis-only GNU extension remains an ISO C17 constraint
// under -pedantic-errors. `int f(int, ...);` is the valid boundary control in
// variadic_and_nested_kr_ok.c.
int unnamed_variadic(...);
