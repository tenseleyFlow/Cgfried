// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format format '%d' expects argument of type 'int'
    printf("%d", "wrong");
}
