// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
_Noreturn void flow_noreturn_target(void);
int flow_ends_noreturn_spec(void)
{
    flow_noreturn_target();
}
