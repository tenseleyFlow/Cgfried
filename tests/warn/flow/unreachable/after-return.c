// FLAGS: -fsyntax-only -Wunreachable-code
// DIVERGES(gcc-8): cgf-only-warning=unreachable-code
// WARN_COUNT: 1
int flow_unreachable_after_return(void)
{
    return 1;
    // WARN_CHECK: unreachable-code code will never be executed
    return 2;
}
