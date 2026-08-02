// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int parentheses_compared(int x, int y)
{
    if ((x = y) != 0)
        return x;
    return 0;
}
