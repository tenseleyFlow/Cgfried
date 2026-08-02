// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): GCC 8 does not recognize the Cgfried infinite-recursion extension.
// WARN_COUNT: 0
int flow_recursive_early_return(int value)
{
    if (value == 0)
        return 0;
    return flow_recursive_early_return(value - 1);
}
