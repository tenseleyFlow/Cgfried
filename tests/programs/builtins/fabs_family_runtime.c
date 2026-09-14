// The three GCC fabs builtins have exact prototypes, evaluate their operand
// once, clear the sign of zero and NaNs, and otherwise preserve every payload
// bit. Long double is x87 f80, IEEE binary128, or binary64 on our three
// targets. FLAGS: -std=gnu17 -Wall -Wextra WARN_COUNT: 0 EXIT_CODE: 0 OPT_EQ:
// all

static float static_f = __builtin_fabsf(-1.25f);
static double static_d = __builtin_fabs(-2.5);
static long double static_l = __builtin_fabsl(-3.75L);
static volatile int calls;

static float source_f(float value)
{
    calls++;
    return value;
}

static double source_d(double value)
{
    calls++;
    return value;
}

static long double source_l(long double value)
{
    calls++;
    return value;
}

static int same_after_sign_clear(const unsigned char *before,
                                 const unsigned char *after, int bytes,
                                 int sign_byte)
{
    int i;

    for (i = 0; i < bytes; i++) {
        unsigned char expected = before[i];

        if (i == sign_byte)
            expected &= 0x7f;
        if (after[i] != expected)
            return 0;
    }
    return 1;
}

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition))                                                      \
            return __LINE__;                                                   \
    } while (0)

int main(void)
{
    union {
        float value;
        unsigned char bytes[sizeof(float)];
    } f_before, f_after;
    union {
        double value;
        unsigned char bytes[sizeof(double)];
    } d_before, d_after;
    union {
        long double value;
        unsigned char bytes[sizeof(long double)];
    } l_before, l_after;
    volatile float vf;
    volatile double vd;
    volatile long double vl;
    int l_bytes = (int)sizeof(long double);
    int l_sign = l_bytes - 1;

#if defined(__x86_64__)
    /* SysV stores the 80 significant bits in the first ten bytes of its
     * 16-byte long-double object; the remaining six are padding. */
    l_bytes = 10;
    l_sign = 9;
#endif

    _Static_assert(_Generic(__builtin_fabsf(-1.0f), float: 1, default: 0),
                   "fabsf returns float");
    _Static_assert(_Generic(__builtin_fabs(-1.0), double: 1, default: 0),
                   "fabs returns double");
    _Static_assert(_Generic(__builtin_fabsl(-1.0L), long double: 1, default: 0),
                   "fabsl returns long double");
    _Static_assert((int)__builtin_fabs(-4.5) == 4, "constant fabs");
    _Static_assert((int)__builtin_fabsl(-5.5L) == 5, "constant fabsl");

    CHECK(static_f == 1.25f);
    CHECK(static_d == 2.5);
    CHECK(static_l == 3.75L);

    calls = 0;
    CHECK(__builtin_fabsf(source_f(-6.0f)) == 6.0f);
    CHECK(__builtin_fabs(source_d(-7.0)) == 7.0);
    CHECK(__builtin_fabsl(source_l(-8.0L)) == 8.0L);
    CHECK(calls == 3);

    vf = -0.0f;
    f_before.value = vf;
    f_after.value = __builtin_fabsf(vf);
    CHECK(same_after_sign_clear(f_before.bytes, f_after.bytes,
                                (int)sizeof(float), 3));

    vd = -__builtin_nan("0x1234");
    d_before.value = vd;
    d_after.value = __builtin_fabs(vd);
    CHECK(same_after_sign_clear(d_before.bytes, d_after.bytes,
                                (int)sizeof(double), 7));

    vl = -__builtin_nanl("0x5678");
    l_before.value = vl;
    l_after.value = __builtin_fabsl(vl);
    CHECK(
        same_after_sign_clear(l_before.bytes, l_after.bytes, l_bytes, l_sign));
    return 0;
}
