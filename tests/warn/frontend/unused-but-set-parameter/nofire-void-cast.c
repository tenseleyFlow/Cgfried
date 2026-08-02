// FLAGS: -fsyntax-only -Wall -Wextra
// WARN_COUNT: 0
int unused_but_set_parameter_void_cast(int p)
{
    p = 1;
    (void)p;
    return 0;
}
