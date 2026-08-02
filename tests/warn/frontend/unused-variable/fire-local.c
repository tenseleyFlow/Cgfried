// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 1
void unused_variable_fire(void)
{
    // WARN_CHECK: unused-variable unused variable 'x'
    int x = 0;
}
