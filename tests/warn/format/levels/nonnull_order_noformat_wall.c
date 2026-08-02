// FLAGS: -fsyntax-only -Wno-format -Wall
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: nonnull argument 1 null where non-null expected
    printf((char *)0);
}
