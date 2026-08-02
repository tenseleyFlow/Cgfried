// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf("errno: %-10.4m"); }
