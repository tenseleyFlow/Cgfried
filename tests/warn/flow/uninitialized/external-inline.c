// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=maybe-uninitialized
// WARN_COUNT: 1
inline int flow_external_inline_uninitialized(int condition)
{
    int x;
    if (condition)
        x = 1;
    // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
    return x;
}
