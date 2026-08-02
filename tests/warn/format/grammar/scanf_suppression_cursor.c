// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
int scanf(const char *, ...);
void test(void)
{
    char out[8];
    scanf("%*d %7s", out);
}
