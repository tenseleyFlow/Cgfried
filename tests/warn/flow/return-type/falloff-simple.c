// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=return-type
// WARN_COUNT: 1
// WARN_CHECK: return-type control reaches end of non-void function
int flow_falloff_simple(void)
{
}
