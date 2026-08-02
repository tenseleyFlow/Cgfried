// FLAGS: -fsyntax-only -Wall
// WARN_COUNT: 0
#define ASSIGN_AND_TEST(a, b) ((a = b))
int parentheses_macro(int x, int y)
{
    if (ASSIGN_AND_TEST(x, y))
        return x;
    return 0;
}
