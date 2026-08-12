// FLAGS: -fsyntax-only -pedantic
// WARNING_EXPECTED: binary constants are a C23 feature or GNU extension
// WARN_COUNT: 1
// WARN_CHECK: pedantic binary constants are a C23 feature or GNU extension
int binary_integer_constant = 0b10;
