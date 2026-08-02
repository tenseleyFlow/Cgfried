// FLAGS: -fsyntax-only -Wformat=2
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf("%u", 1); }
