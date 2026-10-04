// FLAGS: -std=gnu17 -Wall -fsyntax-only
int f(void)
{
    // WARN_CHECK: unused-label label 'lonely' declared but not defined
    __label__ lonely;
    return 0;
}
