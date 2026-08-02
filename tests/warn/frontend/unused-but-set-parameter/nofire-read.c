// FLAGS: -fsyntax-only -Wall -Wextra
// WARN_COUNT: 0
int unused_but_set_parameter_read(int p)
{
    p = 1;
    return p;
}
