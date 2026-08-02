// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
void unused_value_volatile(void)
{
    volatile int x = 0;
    x;
}
