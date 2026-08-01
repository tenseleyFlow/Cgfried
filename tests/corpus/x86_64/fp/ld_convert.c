// OPT_EQ: all
// long double conversions: the RC dance (truncation) and int paths.
// EXIT_CODE: 0
int main(void)
{
    volatile long double x = 2.75L;
    volatile long double xn = -2.75L;
    volatile int i = 7;
    if ((int)x != 2)
        return 1; /* fistp truncates via the dance */
    if ((int)xn != -2)
        return 2;
    if ((long)(x * 4.0L) != 11)
        return 3;
    if ((long double)i != 7.0L)
        return 4;
    if ((double)x != 2.75)
        return 5; /* f80 -> f64 exact here */
    return 0;
}
