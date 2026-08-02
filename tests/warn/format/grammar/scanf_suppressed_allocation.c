// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int scanf(const char *, ...);
void test(void) { scanf("%*ms"); }
