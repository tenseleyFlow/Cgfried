// FLAGS: -fsyntax-only -pedantic-errors
// WARNING_EXPECTED: floating constant exceeds range of 'double'
// WARN_COUNT: 1
// WARN_CHECK: overflow floating constant exceeds range of 'double'
double float_overflow_pedantic_errors = 1e9999;
