// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int printf(const char *, ...);
void test(void)
{
    printf("%1$d %1$u", 1);
}
