// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(int *written)
{
    // WARN_CHECK: format '-' flag used with '%-n' format
    printf("%-n", written);
}
