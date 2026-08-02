// FLAGS: -fsyntax-only -Wall -Wno-format
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void)
{
    printf((char *)0);
}
