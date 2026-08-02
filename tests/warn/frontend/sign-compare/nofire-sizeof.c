// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 0
int sign_compare_sizeof(unsigned long value) { return value < sizeof(int); }
