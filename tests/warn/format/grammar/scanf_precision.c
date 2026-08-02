// FLAGS: -fsyntax-only -Wformat -Wno-format-extra-args
// WARN_COUNT: 1
int scanf(const char *, ...);
void test(void)
{
    char out[8];
    // WARN_CHECK: format unknown conversion type character '.' in format
    scanf("%.5s", out);
}
