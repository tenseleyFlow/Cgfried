// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=maybe-uninitialized
// WARN_COUNT: 1
int flow_predicate_reassigned(int flag)
{
    int x;
    if (flag)
        x = 1;
    flag = !flag;
    if (flag)
        // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
        return x;
    return 0;
}
