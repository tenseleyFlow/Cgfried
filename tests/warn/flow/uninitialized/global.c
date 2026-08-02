// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_global_value;
int flow_global(void)
{
    return flow_global_value;
}
