// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 0
int sign_compare_constant(unsigned int u) { return u < 10; }
