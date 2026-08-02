// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
void test(int choose)
{
    // WARN_CHECK: format format '%s' expects argument of type 'char *'
    printf(choose ? "%d" : "%s", 1);
}
