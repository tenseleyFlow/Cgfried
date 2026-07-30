// FLAGS: --dump-ast
// ERROR_EXPECTED: use of undeclared label
void f(void){ goto nowhere; }
