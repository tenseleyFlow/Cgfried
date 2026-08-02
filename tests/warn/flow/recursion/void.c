// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): cgf-only-warning=infinite-recursion
// WARN_COUNT: 1
// WARN_CHECK: infinite-recursion all paths through this function will call itself
void flow_recursive_void(void)
{
    flow_recursive_void();
}
