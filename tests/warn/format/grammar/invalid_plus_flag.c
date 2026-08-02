// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format '+' flag used with '%+s' format
    printf("%+s", "ok");
}
