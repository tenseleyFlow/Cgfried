// FLAGS: -fsyntax-only -Wtautological-compare
// WARN_COUNT: 1
int self_equal(int value)
{
    // WARN_CHECK: tautological-compare self-comparison always evaluates to a constant
    return value == value;
}
