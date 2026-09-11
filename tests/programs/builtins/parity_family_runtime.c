// The explicit parity families use unsigned int/long/long-long parameter
// widths, return int, and evaluate each argument exactly once.
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

static int reference_parity(unsigned long long value)
{
    int parity = 0;

    while (value != 0) {
        parity ^= (int)(value & 1ull);
        value >>= 1;
    }
    return parity;
}

int main(void)
{
    volatile unsigned long long wide = (1ull << 40) | 1ull;
    unsigned long long sample = 0x9e3779b97f4a7c15ull;
    int bit;

    _Static_assert(__builtin_parity(0u) == 0, "zero");
    _Static_assert(__builtin_parity(7u) == 1, "odd count");
    _Static_assert(__builtin_parity(0xa5a5a5a5u) == 0, "int pattern");
    _Static_assert(__builtin_parityl(1ul << 40) == 1, "long width");
    _Static_assert(__builtin_parityll(0xa5a5a5a5a5a5a5a5ull) == 0,
                   "long long pattern");
    _Static_assert(_Generic(__builtin_parity(1u), int: 1, default: 0),
                   "int result");
    _Static_assert(_Generic(__builtin_parityl(1ul), int: 1, default: 0),
                   "long result");
    _Static_assert(_Generic(__builtin_parityll(1ull), int: 1, default: 0),
                   "long long result");

    if (__builtin_parity(~0u) != 0 || __builtin_parity(-1) != 0)
        return 1;
    if (__builtin_parityl(~0ul) != 0 || __builtin_parityl(-1l) != 0)
        return 2;
    if (__builtin_parityll(~0ull) != 0 || __builtin_parityll(-1ll) != 0)
        return 3;
    if (__builtin_parity(wide) != 1 || __builtin_parityll(wide) != 0)
        return 4;
    if (__builtin_parity(source_int(0xcafeu)) != 1)
        return 5;
    if (__builtin_parityl(source_long(0xcafecafeul)) != 0)
        return 6;
    if (__builtin_parityll(source_llong(0xcafecafecafecafeull)) != 0)
        return 7;
    if (source_calls != 3)
        return 8;
    for (bit = 0; bit < 32; bit++) {
        unsigned value = 1u << bit;

        if (__builtin_parity(value) != 1 || __builtin_parity(~value) != 1)
            return 9;
    }
    for (bit = 0; bit < 64; bit++) {
        unsigned long value = 1ul << bit;

        if (__builtin_parityl(value) != 1 || __builtin_parityl(~value) != 1)
            return 10;
    }
    for (bit = 0; bit < 64; bit++) {
        unsigned long long value = 1ull << bit;

        if (__builtin_parityll(value) != 1 || __builtin_parityll(~value) != 1)
            return 11;
    }
    for (bit = 0; bit < 4096; bit++) {
        sample ^= sample << 13;
        sample ^= sample >> 7;
        sample ^= sample << 17;
        if (__builtin_parityll(sample) != reference_parity(sample) ||
            __builtin_parityl((unsigned long)sample) !=
                reference_parity((unsigned long)sample) ||
            __builtin_parity((unsigned)sample) !=
                reference_parity((unsigned)sample))
            return 12;
    }
    return 0;
}
