// FLAGS: -fsyntax-only -Wlogical-not-parentheses
// WARN_COUNT: 1
int suspicious_not(int value)
{
    // WARN_CHECK: logical-not-parentheses logical not is only applied to the left hand side of comparison
    return !value == 3;
}
