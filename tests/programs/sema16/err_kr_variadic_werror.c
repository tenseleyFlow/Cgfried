// FLAGS: -fsyntax-only -Werror
// ERROR_EXPECTED: defined as variadic function without prototype
void variadic_kr_werror(int, ...);
void variadic_kr_werror(x) int x; { (void)x; }
