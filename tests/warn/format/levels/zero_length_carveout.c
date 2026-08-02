// FLAGS: -fsyntax-only -Wformat -Wno-format-zero-length
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf(""); }
