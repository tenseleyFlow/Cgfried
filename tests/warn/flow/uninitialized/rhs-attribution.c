// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=uninitialized
// WARN_COUNT: 1
int flow_rhs_attribution(void)
{
    int x;
    int y;
    // WARN_CHECK: uninitialized 'y' is used uninitialized in this function
    x = y;
    return x;
}
