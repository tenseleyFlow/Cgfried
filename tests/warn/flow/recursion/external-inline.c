// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): cgf-only-warning=infinite-recursion
// WARN_COUNT: 1
// WARN_CHECK: infinite-recursion all paths through this function will call itself
inline int flow_recursive_external_inline(void)
{
    return flow_recursive_external_inline();
}
