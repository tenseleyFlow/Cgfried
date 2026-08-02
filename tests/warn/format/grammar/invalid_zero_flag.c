// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format '0' flag used with
    printf("%0s", "text");
}
