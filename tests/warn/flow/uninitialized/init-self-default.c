// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_init_self_default(void)
{
    int x = x;
    return x;
}
