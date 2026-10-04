// FLAGS: -std=gnu17 -fsyntax-only
// ERROR_EXPECTED: must precede every ordinary declaration and statement
int f(void)
{
    int value = 0;
    __label__ late;
late:
    return value;
}
