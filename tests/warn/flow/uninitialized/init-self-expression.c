// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=uninitialized
// WARN_COUNT: 1
int flow_init_self_expression(void)
{
    // WARN_CHECK: uninitialized 'x' is used uninitialized in this function
    int x = x + 1;
    return x;
}
