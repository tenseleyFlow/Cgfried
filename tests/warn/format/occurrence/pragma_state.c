// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
#pragma GCC diagnostic ignored "-Wformat"
    printf("%s", 1);
#pragma GCC diagnostic warning "-Wformat"
    // WARN_CHECK: format expects argument of type 'char *'
    printf("%s", 1);
}
