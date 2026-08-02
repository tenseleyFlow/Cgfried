// FLAGS: -fsyntax-only -Wformat -pedantic
// WARN_COUNT: 1
int printf(const char *, ...);
void test(int *pointer)
{
    // WARN_CHECK: format expects argument of type 'void *'
    printf("%p", pointer);
}
