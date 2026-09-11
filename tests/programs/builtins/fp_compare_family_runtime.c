// GCC's ordered/unordered comparison builtins are type-generic over real
// arithmetic operands. They return int, evaluate each operand once, perform
// the usual arithmetic conversions, and keep every ordered predicate false
// when either converted operand is NaN.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

static volatile int calls;
static const double table_nan = 0.0 / 0.0;
static const double table_inf = 1.0 / 0.0;

static double source_d(double value)
{
    calls++;
    return value;
}

static float source_f(float value)
{
    calls++;
    return value;
}

static long double source_l(long double value)
{
    calls++;
    return value;
}

static long double select_less(long double x, long double y, long double yes,
                               long double no)
{
    return __builtin_isless(x, y) ? yes : no;
}

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition))                                                      \
            return __LINE__;                                                   \
    } while (0)

int main(void)
{
    double nan = table_nan;
    double inf = table_inf;

    _Static_assert(
        _Generic(__builtin_isunordered(0.0, 0.0), int: 1, default: 0),
        "unordered returns int");
    _Static_assert(__builtin_isless(1, 2.0), "integer converts to double");
    _Static_assert(__builtin_islessequal(-0.0f, 0.0L),
                   "signed zeros compare equal");
    _Static_assert(__builtin_isgreater(2.0, 1.0), "greater");
    _Static_assert(__builtin_isgreaterequal(2.0L, 2.0f), "greater-equal");
    _Static_assert(__builtin_islessgreater(1.0L, 2.0f), "ordered unequal");
    _Static_assert(!__builtin_islessgreater(2.0, 2.0), "ordered equal");
    _Static_assert(__builtin_isunordered(__builtin_nan(""), 0.0), "NaN");
    _Static_assert(__builtin_isunordered(0.0 / 0.0, 0.0),
                   "constant NaN division");

    CHECK(__builtin_isunordered(nan, 0.0) == 1);
    CHECK(__builtin_isunordered(0.0, nan) == 1);
    CHECK(__builtin_isunordered(0.0, 0.0) == 0);
    CHECK(__builtin_isless(nan, 0.0) == 0);
    CHECK(__builtin_isless(1.0, 2.0) == 1);
    CHECK(__builtin_islessequal(-0.0, 0.0) == 1);
    CHECK(__builtin_isgreater(inf, 1.0) == 1);
    CHECK(__builtin_isgreaterequal(inf, inf) == 1);
    CHECK(__builtin_islessgreater(-inf, inf) == 1);
    CHECK(__builtin_islessgreater(nan, 0.0) == 0);
    CHECK(__builtin_islessgreater(2.0, 2.0) == 0);
    CHECK(select_less(1.0L, 2.0L, 3.0L, 4.0L) == 3.0L);
    CHECK(select_less((long double)nan, 0.0L, 3.0L, 4.0L) == 4.0L);

    calls = 0;
    CHECK(__builtin_isunordered(source_d(nan), source_f(0.0f)) == 1);
    CHECK(__builtin_isless(source_d(1.0), source_l(2.0L)) == 1);
    CHECK(__builtin_islessequal(source_f(2.0f), source_d(2.0)) == 1);
    CHECK(__builtin_isgreater(source_l(3.0L), source_f(2.0f)) == 1);
    CHECK(__builtin_isgreaterequal(source_d(3.0), source_l(3.0L)) == 1);
    CHECK(__builtin_islessgreater(source_f(4.0f), source_d(5.0)) == 1);
    CHECK(calls == 12);
    return 0;
}
