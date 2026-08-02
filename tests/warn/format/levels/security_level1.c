// FLAGS: -fsyntax-only -Wformat=1
// WARN_COUNT: 0
int printf(const char *, ...);
void test(const char *format) { printf(format); }
