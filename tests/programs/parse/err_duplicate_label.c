// FLAGS: --dump-ast
// ERROR_EXPECTED: duplicate label
void f(void){ dup: ; dup: ; }
