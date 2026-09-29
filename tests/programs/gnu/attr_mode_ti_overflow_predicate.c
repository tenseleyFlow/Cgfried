// GNU checked-overflow predicates use the exact unpromoted TI selector range
// while their two operands remain independently typed at no more than 64 bits.
// The selector value is ignored, but its evaluation and side effects remain.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition))                                                      \
            failures++;                                                        \
    } while (0)

_Static_assert(!__builtin_add_overflow_p(~0ULL, ~0ULL, (u128)0),
               "two-limb unsigned addition fits");
_Static_assert(__builtin_sub_overflow_p(0, 1, (u128)0),
               "negative result does not fit unsigned TI");
_Static_assert(!__builtin_mul_overflow_p(~0ULL, ~0ULL, (u128)0),
               "largest 64-bit product fits unsigned TI");
_Static_assert(!__builtin_add_overflow_p(~0ULL, ~0ULL, (i128)0),
               "two-limb signed addition fits");
_Static_assert(__builtin_mul_overflow_p(~0ULL, ~0ULL, (i128)0),
               "largest 64-bit product exceeds signed TI");

static void check_full_width_ranges(void)
{
    const u64 maximum = ~0ULL;
    const i64 minimum = -9223372036854775807LL - 1;
    _Bool overflow;

    overflow = __builtin_add_overflow_p(maximum, maximum, (u128)0);
    CHECK(!overflow);
    overflow = __builtin_sub_overflow_p(0, 1, (u128)0);
    CHECK(overflow);
    overflow = __builtin_add_overflow_p(-5LL, 5ULL, (u128)0);
    CHECK(!overflow);
    overflow = __builtin_add_overflow_p(-5LL, 2ULL, (u128)0);
    CHECK(overflow);
    overflow = __builtin_sub_overflow_p(-5LL, -7LL, (u128)0);
    CHECK(!overflow);
    overflow = __builtin_mul_overflow_p(maximum, maximum, (u128)0);
    CHECK(!overflow);
    overflow = __builtin_mul_overflow_p(-1LL, 2ULL, (u128)0);
    CHECK(overflow);
    overflow = __builtin_mul_overflow_p(-1LL, 0ULL, (u128)0);
    CHECK(!overflow);

    overflow = __builtin_add_overflow_p(maximum, maximum, (i128)0);
    CHECK(!overflow);
    overflow = __builtin_sub_overflow_p(minimum, maximum, (i128)0);
    CHECK(!overflow);
    overflow = __builtin_mul_overflow_p(maximum, maximum, (i128)0);
    CHECK(overflow);
    overflow = __builtin_mul_overflow_p(minimum, maximum, (i128)0);
    CHECK(!overflow);
    overflow = __builtin_mul_overflow_p(minimum, -1LL, (i128)0);
    CHECK(!overflow);
}

struct selector_bits {
    i128 signed_65 : 65;
    u128 unsigned_65 : 65;
};

static struct selector_bits selectors;

static void check_bit_field_ranges(void)
{
    const u64 maximum = ~0ULL;
    const i64 minimum = -9223372036854775807LL - 1;
    _Bool overflow;

    overflow = __builtin_add_overflow_p(maximum, 0, selectors.signed_65);
    CHECK(!overflow);
    overflow = __builtin_add_overflow_p(maximum, 1, selectors.signed_65);
    CHECK(overflow);
    overflow = __builtin_sub_overflow_p(minimum, maximum, selectors.signed_65);
    CHECK(overflow);
    overflow = __builtin_mul_overflow_p(minimum, 2, selectors.signed_65);
    CHECK(!overflow);
    overflow = __builtin_mul_overflow_p(minimum, 3, selectors.signed_65);
    CHECK(overflow);

    overflow =
        __builtin_add_overflow_p(maximum, maximum, selectors.unsigned_65);
    CHECK(!overflow);
    overflow = __builtin_sub_overflow_p(0, 1, selectors.unsigned_65);
    CHECK(overflow);
    overflow = __builtin_mul_overflow_p(maximum, 2, selectors.unsigned_65);
    CHECK(!overflow);
}

static unsigned selector_calls;
static volatile u128 volatile_selector;

static i128 selector_value(void)
{
    selector_calls++;
    return 0;
}

static void check_selector_evaluation(void)
{
    const u64 maximum = ~0ULL;
    _Bool overflow;

    overflow = __builtin_mul_overflow_p(maximum, maximum, selector_value());
    CHECK(overflow && selector_calls == 1);
    overflow = __builtin_sub_overflow_p(0, 1, volatile_selector);
    CHECK(overflow);
}

int main(void)
{
    check_full_width_ranges();
    check_bit_field_ranges();
    check_selector_evaluation();
    return failures != 0;
}
