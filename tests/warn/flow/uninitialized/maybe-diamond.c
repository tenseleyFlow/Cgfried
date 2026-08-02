// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=maybe-uninitialized
// WARN_COUNT: 1
// WARNING_EXPECTED: 'x' is uninitialized when this branch takes the false path
int flow_maybe_diamond(int condition)
{
    int x;
    if (condition)
        x = 1;
    // WARN_CHECK: maybe-uninitialized 'x' may be used uninitialized in this function
    return x;
}
