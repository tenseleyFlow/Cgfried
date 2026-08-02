// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int parentheses_double(int x, int y)
{
    if ((x = y))
        return x;
    return 0;
}
