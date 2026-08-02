// FLAGS: -fsyntax-only -Wunreachable-code
// DIVERGES(gcc-8): cgf-only-warning=unreachable-code
// WARN_COUNT: 1
int flow_unreachable_if_one(void)
{
    if (1)
        return 1;
    else
        // WARN_CHECK: unreachable-code code will never be executed
        return 2;
}
