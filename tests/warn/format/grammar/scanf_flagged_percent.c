// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 2
int scanf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format conversion lacks type at end of format
    scanf("%*%");
}
