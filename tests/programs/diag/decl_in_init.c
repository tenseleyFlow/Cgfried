// FLAGS: -fsyntax-only
// ERROR_EXPECTED: expected
int a[] = { 1, struct s; 2 };
