// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int unused_but_set_read(void)
{
    int x;
    x = 5;
    return x;
}
