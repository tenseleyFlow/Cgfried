// FLAGS: -fsyntax-only
// ERROR_EXPECTED: duplicate label 'twice'
void f(void){ twice: ; twice: ; }
