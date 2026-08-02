// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_ends_builtin_unreachable(void)
{
    __builtin_unreachable();
}
