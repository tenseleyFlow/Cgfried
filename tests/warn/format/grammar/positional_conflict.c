// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format format '%1$s' expects argument of type 'char *'
    printf("%1$d %1$s", 1);
}
