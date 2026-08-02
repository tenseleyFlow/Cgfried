// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=uninitialized
// WARN_COUNT: 1
int flow_definite_condition(void)
{
    int x;
    // WARN_CHECK: uninitialized 'x' is used uninitialized in this function
    if (x)
        return 1;
    return 0;
}
