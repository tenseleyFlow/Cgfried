// FLAGS: -fsyntax-only
// ERROR_EXPECTED: cannot appear on a function
_Alignas(16) int f(void);
