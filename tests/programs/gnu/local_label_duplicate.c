// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: duplicate label declaration 'again'
int f(void)
{
    __label__ again;
    __label__ again;
again:
    return 0;
}
