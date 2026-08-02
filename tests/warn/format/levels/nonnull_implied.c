// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: nonnull null where non-null expected
    printf((const char *)0);
}
