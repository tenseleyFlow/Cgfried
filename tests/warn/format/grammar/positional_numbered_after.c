// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format $ operand number used after format without operand number
    printf("%d %2$d", 1, 2);
}
