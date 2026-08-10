// FLAGS: -E -std=gnu17 -pedantic
// WARNING_EXPECTED: ISO C does not permit named variadic macros
// CHECK: 7
// WARN_CHECK: variadic-macros ISO C does not permit named variadic macros
#define PEDANTIC(rest...) rest
PEDANTIC(7)
