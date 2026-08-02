// FLAGS: -fsyntax-only -Wlogical-not-parentheses
// WARN_COUNT: 0
int compare_truth(int left, int right)
{
    return !left == !right;
}
