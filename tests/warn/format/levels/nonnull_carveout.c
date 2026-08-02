// FLAGS: -fsyntax-only -Wformat -Wno-nonnull
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void) { printf((const char *)0); }
