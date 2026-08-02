// FLAGS: -fsyntax-only -Wformat=0
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf("%d", "unchecked"); }
