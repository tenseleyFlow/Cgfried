// FLAGS: -fsyntax-only -Wsign-compare
// WARN_COUNT: 0

int equality_fitting_constant(int value)
{
    return value == 42u;
}
