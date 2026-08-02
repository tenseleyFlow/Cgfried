// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_uninitialized_if_zero(void)
{
    int x;
    if (0)
        return x;
    return 0;
}
