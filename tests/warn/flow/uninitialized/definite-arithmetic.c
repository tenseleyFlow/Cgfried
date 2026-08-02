// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=uninitialized
// WARN_COUNT: 1
int flow_definite_arithmetic(void)
{
    int x;
    // WARN_CHECK: uninitialized 'x' is used uninitialized in this function
    return x + 1;
}
