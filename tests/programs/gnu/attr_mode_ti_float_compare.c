// GNU floating-comparison builtins convert mode(TI) operands through the
// usual arithmetic conversions, then apply their ordered or unordered
// predicate to the resulting common floating type.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

#define UONE ((u128)1)
#define UBIT(N) (UONE << (N))

_Static_assert(__builtin_isunordered((u128)1, __builtin_nan("")),
               "TI and NaN are unordered");
_Static_assert(__builtin_isless((i128)-1, 0.0), "signed TI is less");
_Static_assert(__builtin_islessequal(UBIT(100), 0x1p100),
               "unsigned TI is less-equal");
_Static_assert(__builtin_isgreater(UBIT(100), 0x1p99),
               "unsigned TI is greater");
_Static_assert(__builtin_isgreaterequal(0x1p100, UBIT(100)),
               "reversed TI is greater-equal");
_Static_assert(__builtin_islessgreater((i128)-2, -1.0),
               "signed TI is ordered and unequal");

static int failures;
static unsigned signed_calls;
static unsigned double_calls;
static volatile u128 volatile_value;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition))                                                      \
            failures++;                                                        \
    } while (0)

static i128 signed_once(void)
{
    signed_calls++;
    return -9;
}

static double double_once(void)
{
    double_calls++;
    return -8.5;
}

int main(void)
{
    i128 minimum = (i128)UBIT(127);
    u128 high = UBIT(127);
    double nan = __builtin_nan("");

    CHECK(__builtin_isunordered(high, nan));
    CHECK(!__builtin_isless(high, nan));
    CHECK(__builtin_isless(minimum, -0x1p126));
    CHECK(__builtin_islessequal(high, 0x1p127));
    CHECK(__builtin_isgreater(high, 0x1p126));
    CHECK(__builtin_isgreaterequal(0x1p127, high));
    CHECK(__builtin_islessgreater((i128)-1, 0.0));
    CHECK(!__builtin_islessgreater(high, nan));

    CHECK(__builtin_isless(signed_once(), double_once()));
    CHECK(signed_calls == 1 && double_calls == 1);

    volatile_value = UBIT(100);
    CHECK(__builtin_isgreater(volatile_value, 0x1p99));
    return failures != 0;
}
