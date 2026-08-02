// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void exit(int);
int flow_ends_exit(void)
{
    exit(1);
}
