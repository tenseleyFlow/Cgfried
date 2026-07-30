// FLAGS: -fsyntax-only
// ERROR_EXPECTED: 'case' label not within a switch statement
void f(void){ case 1: ; }
