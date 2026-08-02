// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 0
int unused_parameter_void_cast(int p)
{
    (void)p;
    return 1;
}
