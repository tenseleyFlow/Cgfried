// FLAGS: -fsyntax-only -Wformat -Wno-format-extra-args
// WARN_COUNT: 1
int printf(const char *, ...);
void test(void)
{
    // WARN_CHECK: format unknown conversion type character 'r' in format
    printf("%r", 1);
}
