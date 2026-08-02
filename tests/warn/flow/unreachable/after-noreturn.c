// FLAGS: -fsyntax-only -Wunreachable-code
// DIVERGES(gcc-8): cgf-only-warning=unreachable-code
// WARN_COUNT: 1
_Noreturn void flow_unreachable_die(void);
int flow_unreachable_after_noreturn(void)
{
    flow_unreachable_die();
    // WARN_CHECK: unreachable-code code will never be executed
    return 1;
}
