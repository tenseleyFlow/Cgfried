// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): cgf-only-warning=infinite-recursion
// WARN_COUNT: 1
// WARN_CHECK: infinite-recursion all paths through this function will call itself
int flow_recursive_call_then_return(void)
{
    flow_recursive_call_then_return();
    return 1;
}
