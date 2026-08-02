// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void unused_variable_void_cast(void)
{
    int x = 0;
    (void)x;
}
