// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int flow_volatile_local(void)
{
    volatile int x;
    return x;
}
