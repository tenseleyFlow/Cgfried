// FLAGS: -fsyntax-only
// WARNING_EXPECTED: defined as variadic function without prototype
void variadic_kr(int, ...);
void variadic_kr(x) int x; { (void)x; }
