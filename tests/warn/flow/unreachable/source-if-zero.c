// FLAGS: -fsyntax-only -Wunreachable-code
// DIVERGES(gcc-8): cgf-only-warning=unreachable-code
// WARN_COUNT: 1
int flow_unreachable_if_zero(void)
{
    if (0)
        // WARN_CHECK: unreachable-code code will never be executed
        return 1;
    return 2;
}
