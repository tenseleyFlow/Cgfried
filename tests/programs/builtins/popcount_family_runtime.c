// The explicit population-count families use unsigned int/long/long-long
// parameter widths, return int, and evaluate each argument exactly once.
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

static int reference_popcount(unsigned long long value)
{
    int count = 0;

    while (value != 0) {
        count += (int)(value & 1ull);
        value >>= 1;
    }
    return count;
}

int main(void)
{
    volatile unsigned long long wide = (1ull << 40) | 3ull;
    unsigned long long sample = 0x9e3779b97f4a7c15ull;
    int bit;

    _Static_assert(__builtin_popcount(0u) == 0, "zero");
    _Static_assert(__builtin_popcount(0xa5a5a5a5u) == 16, "int pattern");
    _Static_assert(__builtin_popcountl(~0ul) == 64, "long width");
    _Static_assert(__builtin_popcountll(0xa5a5a5a5a5a5a5a5ull) == 32,
                   "long long pattern");
    _Static_assert(_Generic(__builtin_popcount(1u), int: 1, default: 0),
                   "int result");
    _Static_assert(_Generic(__builtin_popcountl(1ul), int: 1, default: 0),
                   "long result");
    _Static_assert(_Generic(__builtin_popcountll(1ull), int: 1, default: 0),
                   "long long result");

    if (__builtin_popcount(~0u) != 32 || __builtin_popcount(-1) != 32)
        return 1;
    if (__builtin_popcountl(~0ul) != 64 || __builtin_popcountl(-1l) != 64)
        return 2;
    if (__builtin_popcountll(~0ull) != 64 || __builtin_popcountll(-1ll) != 64)
        return 3;
    if (__builtin_popcount(wide) != 2)
        return 4;
    if (__builtin_popcount(source_int(0xcafeu)) != 11)
        return 5;
    if (__builtin_popcountl(source_long(0xcafecafeul)) != 22)
        return 6;
    if (__builtin_popcountll(source_llong(0xcafecafecafecafeull)) != 44)
        return 7;
    if (source_calls != 3)
        return 8;
    for (bit = 0; bit < 32; bit++) {
        unsigned value = 1u << bit;

        if (__builtin_popcount(value) != 1 || __builtin_popcount(~value) != 31)
            return 9;
    }
    for (bit = 0; bit < 64; bit++) {
        unsigned long value = 1ul << bit;

        if (__builtin_popcountl(value) != 1 ||
            __builtin_popcountl(~value) != 63)
            return 10;
    }
    for (bit = 0; bit < 64; bit++) {
        unsigned long long value = 1ull << bit;

        if (__builtin_popcountll(value) != 1 ||
            __builtin_popcountll(~value) != 63)
            return 11;
    }
    for (bit = 0; bit < 4096; bit++) {
        sample ^= sample << 13;
        sample ^= sample >> 7;
        sample ^= sample << 17;
        if (__builtin_popcountll(sample) != reference_popcount(sample) ||
            __builtin_popcountl((unsigned long)sample) !=
                reference_popcount((unsigned long)sample) ||
            __builtin_popcount((unsigned)sample) !=
                reference_popcount((unsigned)sample))
            return 12;
    }
    return 0;
}
