// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf("%d %s %f", 1, "ok", 2.0); }
