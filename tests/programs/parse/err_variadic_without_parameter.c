// FLAGS: -fsyntax-only
// ERROR_EXPECTED: requires at least one parameter declaration
// FE-H-01: both ISO and GNU C require a parameter declaration before the
// ellipsis. `int f(int, ...);` is the valid boundary control in
// variadic_and_nested_kr_ok.c.
int unnamed_variadic(...);
