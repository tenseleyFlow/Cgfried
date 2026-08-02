// FLAGS: -fsyntax-only -Wbool-compare
// WARN_COUNT: 1
int bool_cannot_equal_two(_Bool value)
{
    // WARN_CHECK: bool-compare comparison of constant with boolean expression is always false
    return value == 2;
}
