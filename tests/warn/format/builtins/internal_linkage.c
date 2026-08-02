// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
static int printf(const char *, ...);
void test(void)
{
    printf("%r");
}
