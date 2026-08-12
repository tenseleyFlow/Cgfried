// FLAGS: -fsyntax-only -Wtautological-compare
// WARN_COUNT: 0

int float_self_comparisons(float f, double d, long double ld)
{
    return (f == f) + (f != f) + (d < d) + (d > d) + (ld <= ld) +
           (ld >= ld);
}
