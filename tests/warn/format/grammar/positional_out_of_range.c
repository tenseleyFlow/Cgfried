// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format operand number out of range in format
    printf("%3$d", 1, 2);
}
