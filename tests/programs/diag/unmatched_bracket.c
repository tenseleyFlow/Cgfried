// FLAGS: -fsyntax-only
// ERROR_EXPECTED: expected ']' after array subscript
int p[4];
void f(void){ p[1 = 0; }
