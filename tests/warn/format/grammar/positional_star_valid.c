// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf("%2$*1$d", 4, 2); }
