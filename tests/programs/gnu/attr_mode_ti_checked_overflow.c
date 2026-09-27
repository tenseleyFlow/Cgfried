// Type-generic checked arithmetic stores exact modulo-2^128 results in TI
// destinations while reporting overflow against the destination's signed or
// unsigned range. TI operands and predicate selectors remain separate
// boundaries.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

_Static_assert(_Generic(__builtin_add_overflow(1, 2, (u128 *)0),
                   _Bool: 1,
                   default: 0),
               "TI-destination checked arithmetic returns _Bool");

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition))                                                      \
            failures++;                                                        \
    } while (0)

static u64 low(u128 value)
{
    return (u64)value;
}

static u64 high(u128 value)
{
    return (u64)(value >> 64);
}

static void check_unsigned_destinations(void)
{
    const u64 maximum = ~0ULL;
    u128 result;
    _Bool overflow;

    overflow = __builtin_add_overflow(maximum, maximum, &result);
    CHECK(!overflow && low(result) == maximum - 1 && high(result) == 1);

    overflow = __builtin_sub_overflow(0ULL, maximum, &result);
    CHECK(overflow && low(result) == 1 && high(result) == maximum);

    overflow = __builtin_mul_overflow(maximum, maximum, &result);
    CHECK(!overflow && low(result) == 1 && high(result) == maximum - 1);

    overflow = __builtin_mul_overflow(-1LL, 2ULL, &result);
    CHECK(overflow && low(result) == maximum - 1 && high(result) == maximum);

    overflow = __builtin_mul_overflow((unsigned char)4, -16, &result);
    CHECK(overflow && low(result) == maximum - 63 && high(result) == maximum);

    overflow = __builtin_mul_overflow(-1LL, 0ULL, &result);
    CHECK(!overflow && result == 0);

    overflow = __builtin_add_overflow(-5LL, 2ULL, &result);
    CHECK(overflow && low(result) == maximum - 2 && high(result) == maximum);

    overflow = __builtin_add_overflow(-5LL, 5ULL, &result);
    CHECK(!overflow && result == 0);

    overflow = __builtin_sub_overflow(-5LL, -7LL, &result);
    CHECK(!overflow && result == 2);

    overflow = __builtin_sub_overflow(-5LL, -5LL, &result);
    CHECK(!overflow && result == 0);
}

static void check_signed_destinations(void)
{
    const u64 maximum = ~0ULL;
    const i64 minimum = -9223372036854775807LL - 1;
    i128 result;
    u128 bits;
    _Bool overflow;

    overflow = __builtin_add_overflow(maximum, maximum, &result);
    bits = (u128)result;
    CHECK(!overflow && low(bits) == maximum - 1 && high(bits) == 1);

    overflow = __builtin_mul_overflow(maximum, maximum, &result);
    bits = (u128)result;
    CHECK(overflow && low(bits) == 1 && high(bits) == maximum - 1);

    overflow = __builtin_mul_overflow(minimum, maximum, &result);
    bits = (u128)result;
    CHECK(!overflow && low(bits) == (1ULL << 63) && high(bits) == (1ULL << 63));

    overflow = __builtin_mul_overflow(minimum, -1LL, &result);
    bits = (u128)result;
    CHECK(!overflow && low(bits) == (1ULL << 63) && high(bits) == 0);

    overflow = __builtin_add_overflow(-5LL, 7ULL, &result);
    CHECK(!overflow && result == 2);

    overflow = __builtin_sub_overflow(-5LL, 7ULL, &result);
    bits = (u128)result;
    CHECK(!overflow && low(bits) == maximum - 11 && high(bits) == maximum);
}

static unsigned calls_left;
static unsigned calls_right;
static unsigned calls_result;
static u128 evaluated_result;
static volatile u128 volatile_result;

static u64 left_value(void)
{
    calls_left++;
    return ~0ULL;
}

static i64 right_value(void)
{
    calls_right++;
    return -1;
}

static u128 *result_pointer(void)
{
    calls_result++;
    return &evaluated_result;
}

static void check_evaluation_and_stores(void)
{
    const u64 maximum = ~0ULL;
    u128 alias = 5;
    _Bool overflow;

    overflow = __builtin_add_overflow((u64)alias, maximum, &alias);
    CHECK(!overflow && low(alias) == 4 && high(alias) == 1);

    overflow =
        __builtin_sub_overflow(left_value(), right_value(), result_pointer());
    CHECK(!overflow && low(evaluated_result) == 0 &&
          high(evaluated_result) == 1);
    CHECK(calls_left == 1 && calls_right == 1 && calls_result == 1);

    overflow = __builtin_mul_overflow(4, -16, &volatile_result);
    CHECK(overflow && low(volatile_result) == maximum - 63 &&
          high(volatile_result) == maximum);
}

int main(void)
{
    check_unsigned_destinations();
    check_signed_destinations();
    check_evaluation_and_stores();
    return failures != 0;
}
