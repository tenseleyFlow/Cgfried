// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
typedef unsigned long size_t;
struct tm;
size_t strftime(char *, size_t, const char *, const struct tm *);
void test(char *out, const struct tm *tm)
{
    // WARN_CHECK: format 'E' modifier used with '%a' format
    strftime(out, 32, "%Ea", tm);
}
