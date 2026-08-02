// FLAGS: -S -Wall
// WARN_COUNT: 1
void unused_but_set_fire(void)
{
    // WARN_CHECK: unused-but-set-variable variable 'x' set but not used
    int x;
    x = 5;
}
