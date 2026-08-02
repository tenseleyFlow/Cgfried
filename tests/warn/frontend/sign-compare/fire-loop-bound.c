// FLAGS: -fsyntax-only -Wextra
// WARN_COUNT: 1
void sign_compare_loop(int n)
{
    // WARN_CHECK: sign-compare comparison of integer expressions of different signedness
    for (unsigned int i = 0; i < n; i++) {}
}
