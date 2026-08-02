// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=maybe-uninitialized
// WARN_COUNT: 1
int flow_maybe_two_predicates(int initialize, int consume)
{
    int x;
    if (initialize)
        x = 1;
    if (consume)
        // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
        return x;
    return 0;
}
