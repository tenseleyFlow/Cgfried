// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=return-type
// WARN_COUNT: 1
// WARNING_EXPECTED: when the false branch is taken, no return statement is reached
// WARN_CHECK: return-type control reaches end of non-void function
int flow_falloff_diamond(int condition)
{
    if (condition)
        return 1;
}
