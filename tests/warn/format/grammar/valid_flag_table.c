// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void)
{
    printf("%-d %#x %+f % e %'u %Id %.3s %4c %p", 1, 1u, 1.0, 1.0, 1u,
           1, "ok", 'x', (void *)0);
}
