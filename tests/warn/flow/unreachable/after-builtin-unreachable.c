// FLAGS: -fsyntax-only -Wunreachable-code
// DIVERGES(gcc-8): cgf-only-warning=unreachable-code
// WARN_COUNT: 1
int flow_after_builtin_unreachable(void)
{
    __builtin_unreachable();
    // WARN_CHECK: unreachable-code code will never be executed
    return 1;
}
