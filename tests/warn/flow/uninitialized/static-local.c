// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_static_local(void)
{
    static int x;
    return x;
}
