// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void quick_exit(int);
int flow_ends_quick_exit(void)
{
    quick_exit(1);
}
