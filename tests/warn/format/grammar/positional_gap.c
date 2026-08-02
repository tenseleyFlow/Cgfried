// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format format argument 1 unused before used argument 2 in $-style format
    printf("%2$d", 1, 2);
}
