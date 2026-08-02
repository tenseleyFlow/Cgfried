// FLAGS: -fsyntax-only -Winfinite-recursion
// DIVERGES(gcc-8): GCC 8 does not recognize the Cgfried infinite-recursion extension.
// WARN_COUNT: 0
_Noreturn void flow_recursive_die(void);
int flow_recursive_noreturn_alternative(int condition)
{
    if (condition)
        return flow_recursive_noreturn_alternative(condition);
    flow_recursive_die();
}
