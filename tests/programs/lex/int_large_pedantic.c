// FLAGS: -fsyntax-only -pedantic
// WARNING_EXPECTED: integer constant is so large that it is unsigned
// WARN_COUNT: 1
// WARN_CHECK: overflow integer constant is so large that it is unsigned
unsigned long long int_large_pedantic = 9223372036854775808;
