// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int scanf(const char *, ...);
void test(void)
{
    char a[8], b[8];
    scanf("%7[]a] %7[^]a]", a, b);
}
