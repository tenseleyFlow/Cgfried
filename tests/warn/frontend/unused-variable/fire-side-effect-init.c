// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 1
extern int make_value(void);
void unused_variable_side_effect(void)
{
    // WARN_CHECK: unused-variable unused variable 'x'
    int x = make_value();
}
