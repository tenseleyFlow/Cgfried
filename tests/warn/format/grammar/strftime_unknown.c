// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
unsigned long strftime(char *, unsigned long, const char *, const void *);
void test(char *out, const void *time)
{
    // WARN_CHECK: format unknown conversion type character 'Q' in format
    strftime(out, 32, "%Q", time);
}
