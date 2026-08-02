// FLAGS: -fsyntax-only -Wswitch-default
// WARN_COUNT: 0
int conditional_only(int value)
{
    if (value)
        return 1;
    return 0;
}
