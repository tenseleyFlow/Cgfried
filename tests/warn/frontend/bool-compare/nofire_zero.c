// FLAGS: -fsyntax-only -Wbool-compare
// WARN_COUNT: 0
int bool_can_equal_zero(_Bool value)
{
    return value == 0;
}
