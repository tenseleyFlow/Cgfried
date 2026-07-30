// FLAGS: -fsyntax-only
// ERROR_EXPECTED: expected ')' after the argument list
int g(int);
void f(void){ g(1; }
