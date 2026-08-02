// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): GCC 8 does not recognize the Cgfried infinite-recursion extension.
// WARN_COUNT: 0
int flow_recursive_loop_before_call(void)
{
    for (;;) {
    }
    return flow_recursive_loop_before_call();
}
