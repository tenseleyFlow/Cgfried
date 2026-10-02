// GNU mode(TI) values retain both limbs through anonymous argument placement
// and __builtin_va_arg on every closed psABI.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));
typedef __builtin_va_list va_list;

#define UONE ((u128)1)
#define UBIT(N) (UONE << (N))
#define UMAX (~(u128)0)
#define IMAX ((i128)(UBIT(127) - UONE))
#define IMIN (-IMAX - 1)

static volatile u128 volatile_value;

static u128 unsigned_after(unsigned count, ...)
{
    va_list ap;
    u128 value;

    __builtin_va_start(ap, count);
    while (count--)
        (void)__builtin_va_arg(ap, unsigned long long);
    value = __builtin_va_arg(ap, u128);
    __builtin_va_end(ap);
    return value;
}

static i128 signed_after(unsigned count, ...)
{
    va_list ap;
    i128 value;

    __builtin_va_start(ap, count);
    while (count--)
        (void)__builtin_va_arg(ap, unsigned long long);
    value = __builtin_va_arg(ap, i128);
    __builtin_va_end(ap);
    return value;
}

static int mixed_stream(int tag, ...)
{
    va_list ap;
    int lead;
    u128 high;
    double fp;
    i128 low;
    int tail;

    __builtin_va_start(ap, tag);
    lead = __builtin_va_arg(ap, int);
    high = __builtin_va_arg(ap, u128);
    fp = __builtin_va_arg(ap, double);
    low = __builtin_va_arg(ap, i128);
    tail = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return tag == 17 && lead == 23 && high == UBIT(100) + (u128)29 &&
           fp == 3.5 && low == IMIN + 31 && tail == 37;
}

int main(void)
{
    u128 high = UBIT(100) + (u128)0x123456789abcdef0ULL;
    i128 low = IMIN + 0x102030405060708LL;

    if (unsigned_after(0, high) != high)
        return 1;
    if (unsigned_after(3, 1ULL, 2ULL, 3ULL, high) != high)
        return 2;
    if (unsigned_after(4, 1ULL, 2ULL, 3ULL, 4ULL, high) != high)
        return 3;
    if (unsigned_after(5, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, high) != high)
        return 4;
    if (unsigned_after(6, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL, high) != high)
        return 5;
    if (unsigned_after(7, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL, 7ULL, high) !=
        high)
        return 6;
    if (unsigned_after(9, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL, 7ULL, 8ULL, 9ULL,
                       UMAX) != UMAX)
        return 7;

    if (signed_after(0, low) != low)
        return 8;
    if (signed_after(6, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL, low) != low)
        return 9;
    if (signed_after(9, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL, 7ULL, 8ULL, 9ULL,
                     (i128)-1) != -1)
        return 10;

    if (!mixed_stream(17, 23, UBIT(100) + (u128)29, 3.5, IMIN + 31, 37))
        return 11;

    volatile_value = UBIT(127) + UBIT(64) + (u128)41;
    if (unsigned_after(0, volatile_value) != volatile_value)
        return 12;
    return 0;
}
