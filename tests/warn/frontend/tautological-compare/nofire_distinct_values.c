// FLAGS: -fsyntax-only -Wtautological-compare
// WARN_COUNT: 0
int distinct_equal(int left, int right)
{
    return left == right;
}
