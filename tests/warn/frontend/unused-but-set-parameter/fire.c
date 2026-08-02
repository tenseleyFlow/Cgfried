// FLAGS: -S -Wall -Wextra
// WARN_COUNT: 1
// WARN_CHECK: unused-but-set-parameter parameter 'p' set but not used
int unused_but_set_parameter_fire(int p)
{
    p = 1;
    return 0;
}
