// FLAGS: -fsyntax-only
// ERROR_EXPECTED: redefinition of 'y'
int y = 1;
int y = 2;
