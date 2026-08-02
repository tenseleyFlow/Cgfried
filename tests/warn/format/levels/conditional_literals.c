// FLAGS: -fsyntax-only -Wformat=2
// WARN_COUNT: 0
int printf(const char *, ...);
void test(int choose) { printf(choose ? "%d" : "%x", 1); }
