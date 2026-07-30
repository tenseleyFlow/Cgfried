// FLAGS: -fsyntax-only
// ERROR_EXPECTED: expected an expression
int a;
void f(void){ a = ; }
