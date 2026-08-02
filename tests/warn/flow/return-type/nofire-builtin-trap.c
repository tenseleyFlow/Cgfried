// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_ends_builtin_trap(void)
{
    __builtin_trap();
}
