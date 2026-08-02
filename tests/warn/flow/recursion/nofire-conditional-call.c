// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): GCC 8 does not recognize the Cgfried infinite-recursion extension.
// WARN_COUNT: 0
int flow_recursive_conditional_call(int condition)
{
    if (condition)
        return flow_recursive_conditional_call(condition);
    return 0;
}
