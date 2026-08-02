// FLAGS: -fsyntax-only -Wtautological-compare
// WARN_COUNT: 0
#define SAME(value) ((value) == (value))
int macro_self_equal(int value)
{
    return SAME(value);
}
