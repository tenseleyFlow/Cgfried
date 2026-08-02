// FLAGS: -fsyntax-only -Wformat -Wformat-signedness
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format expects argument of type 'unsigned int'
    printf("%u", 1);
}
