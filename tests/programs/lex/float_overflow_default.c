// FLAGS: -fsyntax-only
// WARNING_EXPECTED: floating constant exceeds range of 'double'
// WARN_COUNT: 1
// WARN_CHECK: overflow floating constant exceeds range of 'double'
double float_overflow_default = 1e9999;
