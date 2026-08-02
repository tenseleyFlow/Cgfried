// FLAGS: -fsyntax-only -Wall
// DIVERGES(gcc-8): cgf-only-warning=return-type
// WARN_COUNT: 1
static void exit(int code)
{
    (void)code;
}
// WARN_CHECK: return-type control reaches end of non-void function
int flow_static_exit_falloff(void)
{
    exit(0);
}
