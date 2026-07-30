// FLAGS: -fsyntax-only
// ERROR_EXPECTED: not a constant expression
int f(void); enum E { A = f() };
