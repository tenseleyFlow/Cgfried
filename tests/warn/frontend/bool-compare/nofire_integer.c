// FLAGS: -fsyntax-only -Wbool-compare
// WARN_COUNT: 0
int integer_can_equal_two(int value)
{
    return value == 2;
}
