// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int scanf(const char *, ...);
void test(void)
{
    void *const value = 0;
    // WARN_CHECK: format format '%p' expects argument of type 'void **'
    scanf("%p", &value);
}
