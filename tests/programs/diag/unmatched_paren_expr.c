// FLAGS: -fsyntax-only
// ERROR_EXPECTED: expected ')' after parenthesized expression
int a;
void f(void){ a = (1 + 2; }
