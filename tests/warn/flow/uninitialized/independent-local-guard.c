// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=maybe-uninitialized
// WARN_COUNT: 1
int flow_independent_local_guard(int set, int consume)
{
    int x;
    int consume_local = consume;
    if (set)
        x = 1;
    if (consume_local)
        // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
        return x;
    return 0;
}
