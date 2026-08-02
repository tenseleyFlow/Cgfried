// FLAGS: -fsyntax-only -Wlogical-not-parentheses
// WARN_COUNT: 0
int intentional_not(int value, int expected)
{
    return (!value) == expected;
}
