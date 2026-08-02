// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int scanf(const char *, ...);
void test(void)
{
    int out;
    // WARN_CHECK: format 'm' flag used with
    scanf("%md", &out);
}
