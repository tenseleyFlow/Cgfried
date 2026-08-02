// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int scanf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format use of assignment suppression and length modifier together
    scanf("%*ls");
}
