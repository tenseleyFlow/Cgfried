// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 0
long strfmon(char *, unsigned long, const char *, ...);
void test(char *out)
{
    strfmon(out, 32, "%=x!10#2.3Ln", 1.0L);
}
