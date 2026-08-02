// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format missing $ operand number in format
    printf("%1$d %d", 1, 2);
}
