// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): cgf-only-warning=infinite-recursion
// WARN_COUNT: 1
// WARN_CHECK: infinite-recursion all paths through this function will call itself
int flow_recursive_both_branches(int condition)
{
    if (condition)
        return flow_recursive_both_branches(0);
    return flow_recursive_both_branches(1);
}
