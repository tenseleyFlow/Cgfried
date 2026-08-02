// FLAGS: -fsyntax-only -Wparentheses
// WARN_COUNT: 1

int logical_mix_once(int a, int b, int c, int d)
{
    // WARN_CHECK: parentheses suggest parentheses around '&&' within '||'
    return a && b || c && d;
}
