// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int printf(const char *, ...);
void test(unsigned int width) { printf("%*.*f", width, 2, 1.0); }
