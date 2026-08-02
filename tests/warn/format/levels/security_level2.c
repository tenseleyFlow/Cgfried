// FLAGS: -fsyntax-only -Wformat=2
// WARN_COUNT: 1
int printf(const char *, ...);
void test(const char *format)
{
    // WARN_CHECK: format-security format not a string literal and no format arguments
    printf(format);
}
