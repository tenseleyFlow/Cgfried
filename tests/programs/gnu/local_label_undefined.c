// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: use of undeclared label 'missing'
int f(void)
{
    __label__ missing;

    goto missing;
}
