// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 1
int sign_compare_fire(unsigned int u, int s)
{
    // WARN_CHECK: sign-compare comparison of integer expressions of different signedness
    return u < s;
}
