// OPT_EQ: all
// EXIT_CODE: 0

typedef unsigned long long u64;

static int negative_zero_survives(void)
{
    union {
        double d;
        u64 u;
    } z;

    z.d = -0.0 + 0.0;
    /* This expression must be +0: folding it to the left operand is wrong. */
    if (z.u != 0)
        return 1;
    z.d = -0.0 + -0.0;
    /* Adding -0 is the exact identity that is licensed. */
    if (z.u != 0x8000000000000000ull)
        return 2;
    return 0;
}

static int nan_comparison_survives(void)
{
    volatile double zero = 0.0;
    double nan = zero / zero;

    return nan == nan;
}

static int signaling_nan_is_quieted(void)
{
    union {
        double d;
        u64 u;
    } value;
    volatile u64 bits = 0x7ff0000000000001ull;

    value.u = bits;
    value.d = value.d * 1.0;
    if (value.u != 0x7ff8000000000001ull)
        return 4;
    value.u = bits;
    value.d = value.d + -0.0;
    if (value.u != 0x7ff8000000000001ull)
        return 8;
    return 0;
}

static int constant_invalid_nan_bits_match_target(void)
{
    union {
        double d;
        u64 u;
    } value;

    value.d = 0.0 / 0.0;
    return value.u == 0xfff8000000000000ull ? 0 : 16;
}

int main(void)
{
    return negative_zero_survives() + nan_comparison_survives() +
           signaling_nan_is_quieted() +
           constant_invalid_nan_bits_match_target();
}
