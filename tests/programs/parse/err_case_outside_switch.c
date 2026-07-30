// FLAGS: --dump-ast
// ERROR_EXPECTED: 'case' label not within a switch
void f(void){ case 1: ; }
