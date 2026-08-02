// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 1
int parentheses_assignment(int x, int y)
{
    // WARN_CHECK: parentheses suggest parentheses around assignment used as truth value
    if (x = y)
        return x;
    return 0;
}
