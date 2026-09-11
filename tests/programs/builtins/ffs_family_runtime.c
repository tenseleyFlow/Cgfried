// The explicit ffs family uses its int/long/long-long parameter widths,
// returns a one-based bit position (or zero), and evaluates each argument
// exactly once.
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

int main(void)
{
    volatile long long wide = 1LL << 40;

    if (__builtin_ffs(0) != 0 || __builtin_ffs(1) != 1)
        return 1;
    if (__builtin_ffs(-8) != 4)
        return 2;
    if (__builtin_ffs((int)0x80000000u) != 32)
        return 3;
    if (__builtin_ffsl(1L << 40) != 41 || __builtin_ffsl(-1L) != 1)
        return 4;
    if (__builtin_ffsll(1LL << 62) != 63)
        return 5;
    if (__builtin_ffsll(-9223372036854775807LL - 1LL) != 64)
        return 6;
    if (__builtin_ffs(wide) != 0)
        return 7;
    if (__builtin_ffs(source_int(16)) != 5)
        return 8;
    if (__builtin_ffsl(source_long(1L << 32)) != 33)
        return 9;
    if (__builtin_ffsll(source_llong(1LL << 48)) != 49)
        return 10;
    if (source_calls != 3)
        return 11;
    return 0;
}
