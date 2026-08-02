// FLAGS: -fsyntax-only -Wformat -Wformat-unbounded-scanf
// WARN_COUNT: 1
// DIVERGES(gcc-8): cgf-only-warning=format-unbounded-scanf
int scanf(const char *, ...);
void test(void)
{
    char out[8];
    // WARN_CHECK: format-unbounded-scanf unbounded scanf conversion
    scanf("%s", out);
}
