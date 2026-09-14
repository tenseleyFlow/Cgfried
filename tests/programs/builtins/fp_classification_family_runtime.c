// GCC's type-generic real-floating classification builtins preserve the
// operand type, evaluate it once, classify signed zero/NaNs correctly, and
// produce an int truth value.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

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

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition))                                                      \
            return __LINE__;                                                   \
    } while (0)

int main(void)
{
    volatile float fnan = -__builtin_nanf("0x11");
    volatile double dnan = -__builtin_nan("0x22");
    volatile long double lnan = -__builtin_nanl("0x33");
    volatile float finf = __builtin_inff();
    volatile double dinf = __builtin_inf();
    volatile long double linf = __builtin_infl();
    volatile float fzero = -0.0f;
    volatile double dzero = -0.0;
    volatile long double lzero = -0.0L;

    _Static_assert(_Generic(__builtin_isnan(0.0f), int: 1, default: 0),
                   "isnan returns int");
    _Static_assert(_Generic(__builtin_isinf(0.0), int: 1, default: 0),
                   "isinf returns int");
    _Static_assert(_Generic(__builtin_isfinite(0.0L), int: 1, default: 0),
                   "isfinite returns int");
    _Static_assert(_Generic(__builtin_signbit(0.0L), int: 1, default: 0),
                   "signbit returns int");
    _Static_assert(__builtin_isnan(__builtin_nan("")), "constant nan");
    _Static_assert(__builtin_isinf(-__builtin_infl()), "constant infinity");
    _Static_assert(__builtin_isfinite(-1.0f), "constant finite");
    _Static_assert(__builtin_signbit(-0.0L), "constant negative zero");

    CHECK(__builtin_isnan(fnan));
    CHECK(__builtin_isnan(dnan));
    CHECK(__builtin_isnan(lnan));
    CHECK(!__builtin_isnan(finf));
    CHECK(!__builtin_isnan(dinf));
    CHECK(!__builtin_isnan(linf));

    CHECK(__builtin_isinf(finf));
    CHECK(__builtin_isinf(-dinf));
    CHECK(__builtin_isinf(-linf));
    CHECK(!__builtin_isinf(fnan));
    CHECK(!__builtin_isinf(dnan));
    CHECK(!__builtin_isinf(lnan));

    CHECK(__builtin_isfinite(1.0f));
    CHECK(__builtin_isfinite(-2.0));
    CHECK(__builtin_isfinite(3.0L));
    CHECK(!__builtin_isfinite(finf));
    CHECK(!__builtin_isfinite(dnan));
    CHECK(!__builtin_isfinite(linf));

    CHECK(__builtin_signbit(fzero));
    CHECK(__builtin_signbit(dzero));
    CHECK(__builtin_signbit(lzero));
    CHECK(__builtin_signbit(fnan));
    CHECK(__builtin_signbit(dnan));
    CHECK(__builtin_signbit(lnan));
    CHECK(!__builtin_signbit(0.0f));
    CHECK(!__builtin_signbit(__builtin_inf()));
    CHECK(!__builtin_signbit(__builtin_nanl("")));

    calls = 0;
    CHECK(__builtin_isnan(source_f(fnan)));
    CHECK(__builtin_isinf(source_d(dinf)));
    CHECK(__builtin_isfinite(source_l(4.0L)));
    CHECK(__builtin_signbit(source_f(fzero)));
    CHECK(__builtin_signbit(source_d(dzero)));
    CHECK(__builtin_signbit(source_l(lzero)));
    CHECK(calls == 6);
    return 0;
}
