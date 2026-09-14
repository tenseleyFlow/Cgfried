// Type-generic overflow predicates compare the infinite-precision arithmetic
// result with the unpromoted third-argument type, including bit-field widths,
// while evaluating every argument exactly once.
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

_Static_assert(_Generic(__builtin_add_overflow_p(1, 2, 0),
                   _Bool: 1,
                   default: 0),
               "add predicate returns _Bool");
_Static_assert(_Generic(__builtin_sub_overflow_p(1, 2, 0),
                   _Bool: 1,
                   default: 0),
               "sub predicate returns _Bool");
_Static_assert(_Generic(__builtin_mul_overflow_p(1, 2, 0),
                   _Bool: 1,
                   default: 0),
               "mul predicate returns _Bool");

_Static_assert(__builtin_add_overflow_p(127, 1, (signed char)0),
               "signed char addition overflows");
_Static_assert(!__builtin_add_overflow_p(127, 1, (unsigned char)0),
               "unsigned char addition fits");
_Static_assert(__builtin_sub_overflow_p(0, 1, (unsigned char)0),
               "negative result does not fit unsigned char");
_Static_assert(__builtin_mul_overflow_p(-128, -1, (signed char)0),
               "signed char multiplication overflows");
_Static_assert(!__builtin_mul_overflow_p(-128, 1, (signed char)0),
               "signed char minimum fits");
_Static_assert(__builtin_add_overflow_p(~0ULL, 1, 0ULL),
               "unsigned long long maximum plus one overflows");
_Static_assert(!__builtin_add_overflow_p(~0ULL, -1LL, 0ULL),
               "mixed-sign result fits unsigned long long");
_Static_assert(__builtin_add_overflow_p(~0ULL, -1LL, 0LL),
               "mixed-sign result exceeds signed long long");

static void check_narrow_results(void)
{
    int ai;
    int bi;

    for (ai = -128; ai <= 255; ai++) {
        for (bi = -128; bi <= 255; bi++) {
            long long exact;
            _Bool overflow;

            exact = (long long)ai + bi;
            overflow = __builtin_add_overflow_p(ai, bi, (signed char)0);
            CHECK(overflow == (exact < -128 || exact > 127));
            overflow = __builtin_add_overflow_p(ai, bi, (unsigned char)0);
            CHECK(overflow == (exact < 0 || exact > 255));

            exact = (long long)ai - bi;
            overflow = __builtin_sub_overflow_p(ai, bi, (signed char)0);
            CHECK(overflow == (exact < -128 || exact > 127));
            overflow = __builtin_sub_overflow_p(ai, bi, (unsigned char)0);
            CHECK(overflow == (exact < 0 || exact > 255));

            exact = (long long)ai * bi;
            overflow = __builtin_mul_overflow_p(ai, bi, (signed char)0);
            CHECK(overflow == (exact < -128 || exact > 127));
            overflow = __builtin_mul_overflow_p(ai, bi, (unsigned char)0);
            CHECK(overflow == (exact < 0 || exact > 255));
        }
    }
}

static void check_full_width_boundaries(void)
{
    const unsigned long long unsigned_max = ~0ULL;
    const long long signed_max = (long long)(unsigned_max >> 1);
    const long long signed_min = -signed_max - 1;

    CHECK(__builtin_add_overflow_p(unsigned_max, 1ULL, 0ULL));
    CHECK(!__builtin_add_overflow_p(unsigned_max, -1LL, 0ULL));
    CHECK(__builtin_add_overflow_p(unsigned_max, -1LL, 0LL));
    CHECK(__builtin_add_overflow_p(signed_max, 1, 0LL));
    CHECK(!__builtin_sub_overflow_p(signed_min, 0, 0LL));
    CHECK(__builtin_sub_overflow_p(signed_min, 1, 0LL));
    CHECK(__builtin_sub_overflow_p(0ULL, unsigned_max, 0ULL));
    CHECK(__builtin_mul_overflow_p(signed_min, -1, 0LL));
    CHECK(!__builtin_mul_overflow_p(signed_min, 1, 0LL));
    CHECK(__builtin_mul_overflow_p(unsigned_max, 2, 0ULL));
    CHECK(__builtin_mul_overflow_p(-1LL, 2ULL, 0ULL));
    CHECK(!__builtin_mul_overflow_p(-1LL, 0ULL, 0ULL));
}

static unsigned calls_left;
static unsigned calls_right;
static unsigned calls_selector;

static int left_value(void)
{
    calls_left++;
    return 120;
}

static int right_value(void)
{
    calls_right++;
    return 10;
}

static signed char char_selector(void)
{
    calls_selector++;
    return 0;
}

struct bit_fields {
    signed int signed_five : 5;
    unsigned int unsigned_five : 5;
};

static struct bit_fields bit_fields;

static struct bit_fields *field_selector(void)
{
    calls_selector++;
    return &bit_fields;
}

static void check_evaluation_and_bit_fields(void)
{
    _Bool overflow;

    overflow =
        __builtin_add_overflow_p(left_value(), right_value(), char_selector());
    CHECK(overflow);
    CHECK(calls_left == 1 && calls_right == 1 && calls_selector == 1);

    overflow = __builtin_add_overflow_p(15, 1, field_selector()->signed_five);
    CHECK(overflow && calls_selector == 2);
    overflow = __builtin_add_overflow_p(31, 0, field_selector()->unsigned_five);
    CHECK(!overflow && calls_selector == 3);
    overflow = __builtin_add_overflow_p(31, 1, field_selector()->unsigned_five);
    CHECK(overflow && calls_selector == 4);
}

int main(void)
{
    check_narrow_results();
    check_full_width_boundaries();
    check_evaluation_and_bit_fields();
    return failures != 0;
}
