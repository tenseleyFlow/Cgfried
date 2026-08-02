// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 1
int printf(const char *, ...);
// WARN_CHECK: format expects argument of type 'char *'
#define BAD_FORMAT "%s"
void test(void)
{
    printf(BAD_FORMAT, 1);
}
