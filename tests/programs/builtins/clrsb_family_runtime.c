// The explicit redundant-sign-bit families use signed int/long/long-long
// parameter widths, return int, and evaluate each argument exactly once.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all
static volatile int int_input;
static volatile long long_input;
static volatile long long llong_input;
static int source_calls;

static int source_int(int value)
{
    source_calls++;
    int_input = value;
    return int_input;
}

static long source_long(long value)
{
    source_calls++;
    long_input = value;
    return long_input;
}

static long long source_llong(long long value)
{
    source_calls++;
    llong_input = value;
    return llong_input;
}

static int reference_clrsb(unsigned long long bits, int width)
{
    int sign = (int)((bits >> (width - 1)) & 1ull);
    int count = 0;
    int bit;

    for (bit = width - 2; bit >= 0; bit--) {
        if ((int)((bits >> bit) & 1ull) != sign)
            break;
        count++;
    }
    return count;
}

static int reference_int(int value)
{
    return reference_clrsb((unsigned)value, 32);
}

static int reference_long(long value)
{
    return reference_clrsb((unsigned long)value, 64);
}

static int reference_llong(long long value)
{
    return reference_clrsb((unsigned long long)value, 64);
}

int main(void)
{
    volatile long long wide = 1ll << 40;
    unsigned long long sample = 0x9e3779b97f4a7c15ull;
    int bit;

    _Static_assert(__builtin_clrsb(0) == 31, "zero");
    _Static_assert(__builtin_clrsb(-1) == 31, "minus one");
    _Static_assert(__builtin_clrsb(1) == 30, "positive");
    _Static_assert(__builtin_clrsb(-2) == 30, "negative");
    _Static_assert(__builtin_clrsbl(1l << 40) == 22, "long width");
    _Static_assert(__builtin_clrsbll(-9223372036854775807ll - 1ll) == 0,
                   "long long minimum");
    _Static_assert(_Generic(__builtin_clrsb(1), int: 1, default: 0),
                   "int result");
    _Static_assert(_Generic(__builtin_clrsbl(1l), int: 1, default: 0),
                   "long result");
    _Static_assert(_Generic(__builtin_clrsbll(1ll), int: 1, default: 0),
                   "long long result");

    if (__builtin_clrsb(0) != 31 || __builtin_clrsb(-1) != 31)
        return 1;
    if (__builtin_clrsbl(0l) != 63 || __builtin_clrsbl(-1l) != 63)
        return 2;
    if (__builtin_clrsbll(0ll) != 63 || __builtin_clrsbll(-1ll) != 63)
        return 3;
    if (__builtin_clrsb(wide) != 31)
        return 4;
    if (__builtin_clrsb(source_int(0x12345678)) != reference_int(0x12345678))
        return 5;
    if (__builtin_clrsbl(source_long(-0x12345678l)) !=
        reference_long(-0x12345678l))
        return 6;
    if (__builtin_clrsbll(source_llong(0x123456789abcdefll)) !=
        reference_llong(0x123456789abcdefll))
        return 7;
    if (source_calls != 3)
        return 8;
    for (bit = 0; bit < 32; bit++) {
        int value = (int)(1u << bit);

        if (__builtin_clrsb(value) != reference_int(value) ||
            __builtin_clrsb(~value) != reference_int(~value))
            return 9;
    }
    for (bit = 0; bit < 64; bit++) {
        long value = (long)(1ul << bit);

        if (__builtin_clrsbl(value) != reference_long(value) ||
            __builtin_clrsbl(~value) != reference_long(~value))
            return 10;
    }
    for (bit = 0; bit < 64; bit++) {
        long long value = (long long)(1ull << bit);

        if (__builtin_clrsbll(value) != reference_llong(value) ||
            __builtin_clrsbll(~value) != reference_llong(~value))
            return 11;
    }
    for (bit = 0; bit < 4096; bit++) {
        int int_value;
        long long_value;
        long long llong_value;

        sample ^= sample << 13;
        sample ^= sample >> 7;
        sample ^= sample << 17;
        int_value = (int)(unsigned)sample;
        long_value = (long)sample;
        llong_value = (long long)sample;
        if (__builtin_clrsb(int_value) != reference_int(int_value) ||
            __builtin_clrsbl(long_value) != reference_long(long_value) ||
            __builtin_clrsbll(llong_value) != reference_llong(llong_value))
            return 12;
    }
    return 0;
}
