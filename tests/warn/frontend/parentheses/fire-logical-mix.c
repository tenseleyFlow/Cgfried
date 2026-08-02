// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 1
int parentheses_logical(int a, int b, int c)
{
    // WARN_CHECK: parentheses suggest parentheses around '&&' within '||'
    return a && b || c;
}
