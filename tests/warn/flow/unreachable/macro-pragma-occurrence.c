// FLAGS: -fsyntax-only -Wunreachable-code
// DIVERGES(gcc-8): cgf-only-warning=unreachable-code
// WARN_COUNT: 1
// WARN_CHECK: unreachable-code code will never be executed
#define FLOW_BODY return 1; value++
int flow_macro_pragma_occurrence(int value)
{
#pragma GCC diagnostic ignored "-Wunreachable-code"
    if (value > 1) {
        FLOW_BODY;
    }
#pragma GCC diagnostic warning "-Wunreachable-code"
    if (value > 2) {
        FLOW_BODY;
    }
    return 0;
}
