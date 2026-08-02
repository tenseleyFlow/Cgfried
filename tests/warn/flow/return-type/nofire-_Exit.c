// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void _Exit(int);
int flow_ends__Exit(void)
{
    _Exit(1);
}
