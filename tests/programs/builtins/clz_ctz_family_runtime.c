// The explicit clz/ctz families use unsigned int/long/long-long parameter
// widths, return int, and evaluate each defined (nonzero) argument once.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all
static volatile unsigned int_input;
static volatile unsigned long long_input;
static volatile unsigned long long llong_input;
static int source_calls;

static unsigned source_int(unsigned value)
{
    source_calls++;
    int_input = value;
    return int_input;
}

static unsigned long source_long(unsigned long value)
{
    source_calls++;
    long_input = value;
    return long_input;
}

static unsigned long long source_llong(unsigned long long value)
{
    source_calls++;
    llong_input = value;
    return llong_input;
}

int main(void)
{
    volatile unsigned long long wide = (1ull << 40) | 16ull;
    int bit;

    if (__builtin_clz(1u) != 31 || __builtin_ctz(1u) != 0)
        return 1;
    if (__builtin_clz(0x80000000u) != 0 || __builtin_ctz(0x80000000u) != 31)
        return 2;
    if (__builtin_clzl(1ul << 40) != 23 || __builtin_ctzl(1ul << 40) != 40)
        return 3;
    if (__builtin_clzll(1ull << 62) != 1 || __builtin_ctzll(1ull << 62) != 62)
        return 4;
    if (__builtin_clz(wide) != 27 || __builtin_ctz(wide) != 4)
        return 5;
    if (__builtin_clz(source_int(16u)) != 27)
        return 6;
    if (__builtin_ctz(source_int(16u)) != 4)
        return 7;
    if (__builtin_clzl(source_long(1ul << 32)) != 31)
        return 8;
    if (__builtin_ctzl(source_long(1ul << 32)) != 32)
        return 9;
    if (__builtin_clzll(source_llong(1ull << 48)) != 15)
        return 10;
    if (__builtin_ctzll(source_llong(1ull << 48)) != 48)
        return 11;
    if (source_calls != 6)
        return 12;
    for (bit = 0; bit < 32; bit++) {
        unsigned value = 1u << bit;

        if (__builtin_clz(value) != 31 - bit || __builtin_ctz(value) != bit)
            return 13;
    }
    for (bit = 0; bit < 64; bit++) {
        unsigned long value = 1ul << bit;

        if (__builtin_clzl(value) != 63 - bit || __builtin_ctzl(value) != bit)
            return 14;
    }
    for (bit = 0; bit < 64; bit++) {
        unsigned long long value = 1ull << bit;

        if (__builtin_clzll(value) != 63 - bit || __builtin_ctzll(value) != bit)
            return 15;
    }
    return 0;
}
