// Type-generic checked arithmetic preserves the infinite-precision result's
// overflow status while storing its converted low bits through the result
// pointer, including mixed-sign, narrow, aliased, and qualified destinations.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition))                                                      \
            failures++;                                                        \
    } while (0)

_Static_assert(_Generic(__builtin_add_overflow(1, 2, (int *)0),
                   _Bool: 1,
                   default: 0),
               "add overflow returns _Bool");
_Static_assert(_Generic(__builtin_sub_overflow(1, 2, (int *)0),
                   _Bool: 1,
                   default: 0),
               "sub overflow returns _Bool");
_Static_assert(_Generic(__builtin_mul_overflow(1, 2, (int *)0),
                   _Bool: 1,
                   default: 0),
               "mul overflow returns _Bool");

static void check_narrow_results(void)
{
    int ai;
    int bi;

    for (ai = -128; ai <= 255; ai++) {
        for (bi = -128; bi <= 255; bi++) {
            long long exact;
            signed char signed_result;
            unsigned char unsigned_result;
            _Bool overflow;

            exact = (long long)ai + bi;
            overflow = __builtin_add_overflow(ai, bi, &signed_result);
            CHECK(overflow == (exact < -128 || exact > 127));
            CHECK((unsigned char)signed_result == (unsigned char)exact);
            overflow = __builtin_add_overflow(ai, bi, &unsigned_result);
            CHECK(overflow == (exact < 0 || exact > 255));
            CHECK(unsigned_result == (unsigned char)exact);

            exact = (long long)ai - bi;
            overflow = __builtin_sub_overflow(ai, bi, &signed_result);
            CHECK(overflow == (exact < -128 || exact > 127));
            CHECK((unsigned char)signed_result == (unsigned char)exact);
            overflow = __builtin_sub_overflow(ai, bi, &unsigned_result);
            CHECK(overflow == (exact < 0 || exact > 255));
            CHECK(unsigned_result == (unsigned char)exact);

            exact = (long long)ai * bi;
            overflow = __builtin_mul_overflow(ai, bi, &signed_result);
            CHECK(overflow == (exact < -128 || exact > 127));
            CHECK((unsigned char)signed_result == (unsigned char)exact);
            overflow = __builtin_mul_overflow(ai, bi, &unsigned_result);
            CHECK(overflow == (exact < 0 || exact > 255));
            CHECK(unsigned_result == (unsigned char)exact);
        }
    }
}

static void check_full_width_boundaries(void)
{
    const unsigned long long unsigned_max = ~0ULL;
    const long long signed_max = (long long)(unsigned_max >> 1);
    const long long signed_min = -signed_max - 1;
    unsigned long long unsigned_result;
    long long signed_result;

    CHECK(__builtin_add_overflow(unsigned_max, 1ULL, &unsigned_result));
    CHECK(unsigned_result == 0);
    CHECK(!__builtin_add_overflow(unsigned_max, -1LL, &unsigned_result));
    CHECK(unsigned_result == unsigned_max - 1);
    CHECK(__builtin_add_overflow(unsigned_max, -1LL, &signed_result));
    CHECK((unsigned long long)signed_result == unsigned_max - 1);
    CHECK(__builtin_add_overflow(signed_max, 1, &signed_result));
    CHECK(signed_result == signed_min);

    CHECK(!__builtin_sub_overflow(signed_min, 0, &signed_result));
    CHECK(signed_result == signed_min);
    CHECK(__builtin_sub_overflow(signed_min, 1, &signed_result));
    CHECK(signed_result == signed_max);
    CHECK(__builtin_sub_overflow(0ULL, unsigned_max, &unsigned_result));
    CHECK(unsigned_result == 1);

    CHECK(__builtin_mul_overflow(signed_min, -1, &signed_result));
    CHECK(signed_result == signed_min);
    CHECK(!__builtin_mul_overflow(signed_min, 1, &signed_result));
    CHECK(signed_result == signed_min);
    CHECK(__builtin_mul_overflow(unsigned_max, 2, &unsigned_result));
    CHECK(unsigned_result == unsigned_max - 1);
    CHECK(__builtin_mul_overflow(-1LL, 2ULL, &unsigned_result));
    CHECK(unsigned_result == unsigned_max - 1);
    CHECK(!__builtin_mul_overflow(-1LL, 0ULL, &unsigned_result));
    CHECK(unsigned_result == 0);
}

static unsigned calls_left;
static unsigned calls_right;
static unsigned calls_result;
static unsigned alias_result;

static unsigned left_value(void)
{
    calls_left++;
    return ~0U;
}

static unsigned right_value(void)
{
    calls_right++;
    return 1;
}

static unsigned *result_pointer(void)
{
    calls_result++;
    return &alias_result;
}

static void check_evaluation_and_qualified_store(void)
{
    unsigned alias = ~0U;
    volatile unsigned short volatile_result = 0;
    _Bool overflow;

    overflow = __builtin_add_overflow(alias, 1U, &alias);
    CHECK(overflow && alias == 0);

    overflow =
        __builtin_add_overflow(left_value(), right_value(), result_pointer());
    CHECK(overflow && alias_result == 0);
    CHECK(calls_left == 1 && calls_right == 1 && calls_result == 1);

    overflow = __builtin_sub_overflow(0, 1, &volatile_result);
    CHECK(overflow && volatile_result == (unsigned short)-1);
}

int main(void)
{
    check_narrow_results();
    check_full_width_boundaries();
    check_evaluation_and_qualified_store();
    return failures != 0;
}
