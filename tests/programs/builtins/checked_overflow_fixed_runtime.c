// Fixed-prototype checked arithmetic converts both operands and the result
// pointer through the builtin's declared type before doing fully defined
// infinite-precision arithmetic. Cover every signedness/rank/operation row.
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

#define ASSERT_BOOL(expression, message)                                       \
    _Static_assert(_Generic((expression), _Bool: 1, default: 0), message)

ASSERT_BOOL((__builtin_sadd_overflow(1, 2, (int *)0)), "sadd type");
ASSERT_BOOL((__builtin_saddl_overflow(1, 2, (long *)0)), "saddl type");
ASSERT_BOOL((__builtin_saddll_overflow(1, 2, (long long *)0)), "saddll type");
ASSERT_BOOL((__builtin_uadd_overflow(1, 2, (unsigned int *)0)), "uadd type");
ASSERT_BOOL((__builtin_uaddl_overflow(1, 2, (unsigned long *)0)), "uaddl type");
ASSERT_BOOL((__builtin_uaddll_overflow(1, 2, (unsigned long long *)0)),
            "uaddll type");
ASSERT_BOOL((__builtin_ssub_overflow(1, 2, (int *)0)), "ssub type");
ASSERT_BOOL((__builtin_ssubl_overflow(1, 2, (long *)0)), "ssubl type");
ASSERT_BOOL((__builtin_ssubll_overflow(1, 2, (long long *)0)), "ssubll type");
ASSERT_BOOL((__builtin_usub_overflow(1, 2, (unsigned int *)0)), "usub type");
ASSERT_BOOL((__builtin_usubl_overflow(1, 2, (unsigned long *)0)), "usubl type");
ASSERT_BOOL((__builtin_usubll_overflow(1, 2, (unsigned long long *)0)),
            "usubll type");
ASSERT_BOOL((__builtin_smul_overflow(1, 2, (int *)0)), "smul type");
ASSERT_BOOL((__builtin_smull_overflow(1, 2, (long *)0)), "smull type");
ASSERT_BOOL((__builtin_smulll_overflow(1, 2, (long long *)0)), "smulll type");
ASSERT_BOOL((__builtin_umul_overflow(1, 2, (unsigned int *)0)), "umul type");
ASSERT_BOOL((__builtin_umull_overflow(1, 2, (unsigned long *)0)), "umull type");
ASSERT_BOOL((__builtin_umulll_overflow(1, 2, (unsigned long long *)0)),
            "umulll type");

#define CHECK_SIGNED(TYPE, UTYPE, ADD, SUB, MUL)                               \
    do {                                                                       \
        const UTYPE unsigned_max = (UTYPE) ~(UTYPE)0;                          \
        const TYPE maximum = (TYPE)(unsigned_max >> 1);                        \
        const TYPE minimum = (TYPE)(-maximum - 1);                             \
        TYPE result;                                                           \
        CHECK(ADD(maximum, 1, &result) && result == minimum);                  \
        CHECK(ADD(minimum, -1, &result) && result == maximum);                 \
        CHECK(!ADD(maximum, -1, &result) && result == maximum - 1);            \
        CHECK(SUB(minimum, 1, &result) && result == maximum);                  \
        CHECK(SUB(maximum, -1, &result) && result == minimum);                 \
        CHECK(!SUB(minimum, -1, &result) && result == minimum + 1);            \
        CHECK(MUL(maximum, 2, &result) && result == (TYPE) - 2);               \
        CHECK(MUL(minimum, -1, &result) && result == minimum);                 \
        CHECK(!MUL(minimum, 1, &result) && result == minimum);                 \
        CHECK(!MUL(0, minimum, &result) && result == 0);                       \
    } while (0)

#define CHECK_UNSIGNED(TYPE, ADD, SUB, MUL)                                    \
    do {                                                                       \
        const TYPE maximum = (TYPE) ~(TYPE)0;                                  \
        TYPE result;                                                           \
        CHECK(ADD(maximum, 1, &result) && result == 0);                        \
        CHECK(!ADD(maximum, 0, &result) && result == maximum);                 \
        CHECK(SUB(0, 1, &result) && result == maximum);                        \
        CHECK(!SUB(maximum, maximum, &result) && result == 0);                 \
        CHECK(MUL(maximum, 2, &result) && result == maximum - 1);              \
        CHECK(!MUL(maximum, 1, &result) && result == maximum);                 \
        CHECK(!MUL(0, maximum, &result) && result == 0);                       \
    } while (0)

static void check_all_boundaries(void)
{
    CHECK_SIGNED(int, unsigned int, __builtin_sadd_overflow,
                 __builtin_ssub_overflow, __builtin_smul_overflow);
    CHECK_SIGNED(long, unsigned long, __builtin_saddl_overflow,
                 __builtin_ssubl_overflow, __builtin_smull_overflow);
    CHECK_SIGNED(long long, unsigned long long, __builtin_saddll_overflow,
                 __builtin_ssubll_overflow, __builtin_smulll_overflow);
    CHECK_UNSIGNED(unsigned int, __builtin_uadd_overflow,
                   __builtin_usub_overflow, __builtin_umul_overflow);
    CHECK_UNSIGNED(unsigned long, __builtin_uaddl_overflow,
                   __builtin_usubl_overflow, __builtin_umull_overflow);
    CHECK_UNSIGNED(unsigned long long, __builtin_uaddll_overflow,
                   __builtin_usubll_overflow, __builtin_umulll_overflow);
}

static void check_prototype_conversions(void)
{
    int si;
    long sl;
    long long sll;
    unsigned int ui;
    unsigned long ul;
    unsigned long long ull;

    CHECK(!__builtin_sadd_overflow(1.9, 2.9, &si) && si == 3);
    CHECK(!__builtin_saddl_overflow(1.9, 2.9, &sl) && sl == 3);
    CHECK(!__builtin_saddll_overflow(1.9, 2.9, &sll) && sll == 3);

    /* -1 converts to each unsigned parameter's maximum BEFORE addition.
     * The type-generic family would instead compute exact -1 + 1 == 0 and
     * report no overflow for the same unsigned result destination. */
    CHECK(__builtin_uadd_overflow(-1, 1, &ui) && ui == 0);
    CHECK(__builtin_uaddl_overflow(-1, 1, &ul) && ul == 0);
    CHECK(__builtin_uaddll_overflow(-1, 1, &ull) && ull == 0);
}

static unsigned calls_left;
static unsigned calls_right;
static unsigned calls_result;
static int evaluated_result;

static int left_value(void)
{
    calls_left++;
    return (int)(~0U >> 1);
}

static int right_value(void)
{
    calls_right++;
    return 1;
}

static int *result_pointer(void)
{
    calls_result++;
    return &evaluated_result;
}

static void check_evaluation_and_aliasing(void)
{
    int alias = (int)(~0U >> 1);
    const int minimum = -alias - 1;
    _Bool overflow;

    overflow = __builtin_sadd_overflow(alias, 1, &alias);
    CHECK(overflow && alias == minimum);

    overflow =
        __builtin_sadd_overflow(left_value(), right_value(), result_pointer());
    CHECK(overflow && evaluated_result == minimum);
    CHECK(calls_left == 1 && calls_right == 1 && calls_result == 1);
}

int main(void)
{
    check_all_boundaries();
    check_prototype_conversions();
    check_evaluation_and_aliasing();
    return failures != 0;
}
