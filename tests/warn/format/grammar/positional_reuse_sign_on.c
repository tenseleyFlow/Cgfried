// FLAGS: -fsyntax-only -Wformat -Wformat-signedness
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format format '%1$u' expects argument of type 'unsigned int'
    printf("%1$d %1$u", 1);
}
