// FLAGS: -fsyntax-only -Wformat=2
// WARN_COUNT: 3
unsigned long strftime(char *, unsigned long, const char *, const void *);
void test(char *out, const void *time)
{
    // WARN_CHECK: format-y2k format may yield only last 2 digits of year
    strftime(out, 16, "%y", time);
    // WARN_CHECK: format-y2k format may yield only last 2 digits of year
    strftime(out, 16, "%c", time);
    // WARN_CHECK: format-y2k format may yield only last 2 digits of year
    strftime(out, 16, "%x", time);
}
