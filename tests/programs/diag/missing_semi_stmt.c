// FLAGS: -fsyntax-only
// ERROR_EXPECTED: expected ';' after an expression
int a, b;
void f(void){ a = 1 b = 2; }
