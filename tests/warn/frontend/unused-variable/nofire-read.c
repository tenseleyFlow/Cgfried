// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
int unused_variable_read(void)
{
    int x = 3;
    return x;
}
