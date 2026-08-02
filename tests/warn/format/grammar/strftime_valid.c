// FLAGS: -fsyntax-only -Wformat=2
// WARN_COUNT: 0
unsigned long strftime(char *, unsigned long, const char *, const void *);
void test(char *out, const void *time)
{
    strftime(out, 32, "%Y-%m-%d %H:%M:%S", time);
}
