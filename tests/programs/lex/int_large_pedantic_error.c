// FLAGS: -fsyntax-only -pedantic-errors
// ERROR_EXPECTED: integer constant is so large that it is unsigned
// ERROR_EXPECTED: [-Werror=overflow]
unsigned long long int_large_pedantic_error = 9223372036854775808;
