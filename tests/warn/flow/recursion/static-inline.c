// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): cgf-only-warning=infinite-recursion
// WARN_COUNT: 1
// WARN_CHECK: infinite-recursion all paths through this function will call itself
static inline int flow_recursive_static_inline(void)
{
    return flow_recursive_static_inline();
}
int flow_recursive_static_inline_use(void)
{
    return flow_recursive_static_inline();
}
