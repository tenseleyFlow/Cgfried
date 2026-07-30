// FLAGS: -fsyntax-only
// ERROR_EXPECTED: static declaration of 'x' follows non-static
int x;
static int x;
