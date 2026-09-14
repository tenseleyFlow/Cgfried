// The three GCC copysign builtins have exact prototypes, evaluate both
// operands once, and copy only the sign bit for zero, infinity, and NaNs.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

static float static_f = __builtin_copysignf(1.25f, -0.0f);
static double static_d = __builtin_copysign(-2.5, 0.0);
static long double static_l = __builtin_copysignl(3.75L, -1.0L);
static volatile int magnitude_calls;
static volatile int sign_calls;

static float magnitude_f(float value)
{
    magnitude_calls++;
    return value;
}

static float sign_f(float value)
{
    sign_calls++;
    return value;
}

static double magnitude_d(double value)
{
    magnitude_calls++;
    return value;
}

static double sign_d(double value)
{
    sign_calls++;
    return value;
}

static long double magnitude_l(long double value)
{
    magnitude_calls++;
    return value;
}

static long double sign_l(long double value)
{
    sign_calls++;
    return value;
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
        unsigned int bits;
    } f;
    union {
        double value;
        unsigned long long bits;
    } d;
    volatile float vf;
    volatile double vd;
    volatile long double vl;

    _Static_assert(sizeof(float) == sizeof(unsigned int), "float carrier");
    _Static_assert(sizeof(double) == sizeof(unsigned long long),
                   "double carrier");
    _Static_assert(_Generic(__builtin_copysignf(1, -1), float: 1, default: 0),
                   "copysignf returns float");
    _Static_assert(_Generic(__builtin_copysign(1, -1), double: 1, default: 0),
                   "copysign returns double");
    _Static_assert(
        _Generic(__builtin_copysignl(1, -1), long double: 1, default: 0),
        "copysignl returns long double");
    _Static_assert((int)__builtin_copysign(-4.5, 1.0) == 4,
                   "constant copysign");
    _Static_assert((int)__builtin_copysignl(5.5L, -1.0L) == -5,
                   "constant copysignl");
    _Static_assert(__builtin_constant_p(__builtin_copysign(-1.0, 1.0)),
                   "copysign is folded");

    CHECK(static_f == -1.25f);
    CHECK(static_d == 2.5);
    CHECK(static_l == -3.75L);

    magnitude_calls = 0;
    sign_calls = 0;
    CHECK(__builtin_copysignf(magnitude_f(-6.0f), sign_f(1.0f)) == 6.0f);
    CHECK(__builtin_copysign(magnitude_d(7.0), sign_d(-1.0)) == -7.0);
    CHECK(__builtin_copysignl(magnitude_l(-8.0L), sign_l(-1.0L)) == -8.0L);
    CHECK(magnitude_calls == 3);
    CHECK(sign_calls == 3);

    vf = __builtin_copysignf(0.0f, -1.0f);
    CHECK(vf == 0.0f && __builtin_signbit(vf));
    vf = __builtin_copysignf(-0.0f, 1.0f);
    CHECK(vf == 0.0f && !__builtin_signbit(vf));
    vd = __builtin_copysign(__builtin_inf(), -0.0);
    CHECK(__builtin_isinf(vd) && __builtin_signbit(vd));
    vl = __builtin_copysignl(-__builtin_infl(), 0.0L);
    CHECK(__builtin_isinf(vl) && !__builtin_signbit(vl));

    /* Copying the sign must leave every non-sign payload bit untouched. */
    f.bits = 0x7fc12345U;
    f.value = __builtin_copysignf(f.value, -1.0f);
    CHECK(f.bits == 0xffc12345U);
    CHECK(__builtin_isnan(f.value) && __builtin_signbit(f.value));
    d.bits = 0xfff8123456789abcULL;
    d.value = __builtin_copysign(d.value, 1.0);
    CHECK(d.bits == 0x7ff8123456789abcULL);
    CHECK(__builtin_isnan(d.value) && !__builtin_signbit(d.value));

    vl = __builtin_copysignl(__builtin_nanl("0x5678"), -0.0L);
    CHECK(__builtin_isnan(vl) && __builtin_signbit(vl));
    return 0;
}
