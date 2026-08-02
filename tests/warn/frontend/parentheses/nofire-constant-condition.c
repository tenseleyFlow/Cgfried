// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int parentheses_constant(void)
{
    if (1)
        return 1;
    return 0;
}
