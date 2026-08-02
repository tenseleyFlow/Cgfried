// FLAGS: -fsyntax-only -Wformat=1
// WARN_COUNT: 0
unsigned long strftime(char *, unsigned long, const char *, const void *);
void test(char *out, const void *time)
{
    strftime(out, 16, "%y %c %x", time);
}
