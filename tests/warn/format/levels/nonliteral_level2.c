// FLAGS: -fsyntax-only -Wformat=2
// WARN_COUNT: 1
int printf(const char *, ...);
void test(const char *format)
{
    // WARN_CHECK: format-nonliteral format not a string literal, argument types not checked
    printf(format, 1);
}
