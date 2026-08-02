// FLAGS: -fsyntax-only -Wformat -Wno-format-extra-args
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf("fixed", 1); }
