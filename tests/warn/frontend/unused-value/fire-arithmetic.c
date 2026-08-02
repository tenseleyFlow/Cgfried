// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 1
void unused_value_fire(int x)
{
    // WARN_CHECK: unused-value expression result unused
    x + 1;
}
