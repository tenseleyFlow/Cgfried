// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
_Noreturn void flow_die(void);
int flow_noreturn_path(void)
{
    int x;
    flow_die();
    return x;
}
