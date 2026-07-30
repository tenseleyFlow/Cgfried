// FLAGS: -fsyntax-only
// ERROR_EXPECTED: use of undeclared label 'gone'
void f(void){ goto gone; }
