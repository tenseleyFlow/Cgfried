// FLAGS: -fsyntax-only -Wformat -Wno-format-contains-nul -Wno-format-extra-args
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf("prefix\0%d", 1); }
