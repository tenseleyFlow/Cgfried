// FLAGS: -fsyntax-only
// ERROR_EXPECTED: 'continue' outside of a loop
void f(void){ continue; }
