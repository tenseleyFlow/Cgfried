#include <string.h>

#include "unit.h"
#include "util/base.h"
#include "util/softfp.h"

/* The compile-time float engine.
 *
 * Every expectation here is a BIT PATTERN, never a comparison against a
 * host value — the whole reason this engine exists is that the host's
 * answer is not necessarily the target's, and a test that asked the host
 * would be testing the thing it is meant to replace. The patterns were
 * taken from the IEEE-754 definitions and cross-checked against gcc by
 * scripts/fp_diff.sh, which is where broad coverage lives; this file
 * pins the cases that a differential cannot express (intermediate
 * status flags, format conversion, ordering). */

static uint64_t bits64(Sf v)
{
    uint8_t b[16];
    uint64_t u = 0;
    int i;

    sf_to_bits(v, SF_BINARY64, b);
    for (i = 7; i >= 0; i--)
        u = (u << 8) | b[i];
    return u;
}

static uint32_t bits32(Sf v)
{
    uint8_t b[16];
    uint32_t u = 0;
    int i;

    sf_to_bits(v, SF_BINARY32, b);
    for (i = 3; i >= 0; i--)
        u = (u << 8) | b[i];
    return u;
}

static Sf dec(const char *digits, int32_t e, SfFormat f)
{
    SfStatus st;

    memset(&st, 0, sizeof(st));
    return sf_from_decimal(digits, strlen(digits), e, f, &st);
}

static Sf d64(const char *digits, int32_t e)
{
    return dec(digits, e, SF_BINARY64);
}

static void d64_is(TestCtx *t, const char *digits, int32_t e, uint64_t want,
                   const char *why)
{
    uint64_t got = bits64(d64(digits, e));

    if (got != want)
        t_fail(t, __FILE__, __LINE__, "%se%d: 0x%016llX, want 0x%016llX (%s)",
               digits, (int)e, (unsigned long long)got,
               (unsigned long long)want, why);
    t->assertions++;
}

/* --- the torture table, in binary64 -------------------------------------- */

void test_softfp_torture_binary64(TestCtx *t)
{
    /* The smallest positive double. A conversion that rounds through an
     * intermediate format collapses this to zero. */
    d64_is(t, "5", -324, 0x0000000000000001ull, "min subnormal");
    /* The literal that hung PHP and Java: just below DBL_MIN, so it is
     * the LARGEST subnormal, and host x87 double-rounds it. */
    d64_is(t, "22250738585072011", -324, 0x000FFFFFFFFFFFFFull,
           "largest subnormal");
    d64_is(t, "22250738585072014", -324, 0x0010000000000000ull,
           "DBL_MIN itself is normal");
    /* Round-to-nearest-even on the classic tie. */
    d64_is(t, "1", -1, 0x3FB999999999999Aull, "0.1");
    d64_is(t, "10000000000000002", -16, 0x3FF0000000000001ull,
           "1 ulp above 1.0");
    /* 2^53 + 1 is not representable; it rounds to 2^53 (ties to even). */
    d64_is(t, "9007199254740993", 0, 0x4340000000000000ull, "2^53 + 1");
    d64_is(t, "9007199254740992", 0, 0x4340000000000000ull, "2^53 exactly");
    d64_is(t, "9007199254740995", 0, 0x4340000000000002ull,
           "2^53 + 3 rounds up to an even significand");

    /* Exact values, where nothing may drift. */
    d64_is(t, "0", 0, 0x0000000000000000ull, "zero");
    d64_is(t, "1", 0, 0x3FF0000000000000ull, "1.0");
    d64_is(t, "2", 0, 0x4000000000000000ull, "2.0");
    d64_is(t, "5", -1, 0x3FE0000000000000ull, "0.5");
    d64_is(t, "25", -2, 0x3FD0000000000000ull, "0.25");
    d64_is(t, "3", 0, 0x4008000000000000ull, "3.0");
    d64_is(t, "1024", 0, 0x4090000000000000ull, "1024.0");

    /* Transcendental constants: every mantissa bit matters. */
    d64_is(t, "3141592653589793", -15, 0x400921FB54442D18ull, "pi");
    d64_is(t, "2718281828459045", -15, 0x4005BF0A8B145769ull, "e");
    d64_is(t, "14142135623730951", -16, 0x3FF6A09E667F3BCDull, "sqrt 2");
    d64_is(t, "3333333333333333", -16, 0x3FD5555555555555ull, "1/3");

    /* The range boundaries. */
    d64_is(t, "17976931348623157", 292, 0x7FEFFFFFFFFFFFFFull, "DBL_MAX");
}

void test_softfp_torture_binary32(TestCtx *t)
{
    struct {
        const char *digits;
        int32_t e;
        uint32_t want;
        const char *why;
    } rows[] = {
        {"1", 0, 0x3F800000u, "1.0f"},
        {"2", 0, 0x40000000u, "2.0f"},
        {"5", -1, 0x3F000000u, "0.5f"},
        {"1", -1, 0x3DCCCCCDu, "0.1f rounds up"},
        {"3141592653589793", -15, 0x40490FDBu, "pi as float"},
        {"0", 0, 0x00000000u, "zero"},
        /* FLT_MIN and the smallest subnormal float. */
        {"11754943508222875", -54, 0x00800000u, "FLT_MIN"},
        {"1401298464324817", -60, 0x00000001u, "smallest subnormal float"},
        {"34028234663852886", 22, 0x7F7FFFFFu, "FLT_MAX"},
        /* Overflow past FLT_MAX becomes infinity. */
        {"1", 39, 0x7F800000u, "1e39 overflows float"},
        /* 2^24 + 1 is not representable in binary32. */
        {"16777217", 0, 0x4B800000u, "2^24 + 1 ties to even"},
        {"16777219", 0, 0x4B800002u, "2^24 + 3 rounds up"},
    };
    size_t i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        uint32_t got = bits32(dec(rows[i].digits, rows[i].e, SF_BINARY32));

        if (got != rows[i].want)
            t_fail(t, __FILE__, __LINE__, "%se%d: 0x%08X, want 0x%08X (%s)",
                   rows[i].digits, (int)rows[i].e, got, rows[i].want,
                   rows[i].why);
        t->assertions++;
    }
}

/* x87-80 and binary128 have no host type to compare against on most
 * machines, which is exactly why they need pinned patterns. */
void test_softfp_wide_formats(TestCtx *t)
{
    SfStatus st;
    uint8_t b[16];
    Sf v;

    memset(&st, 0, sizeof(st));
    /* 1.0 in x87 80-bit: the leading one is EXPLICIT, so the significand
     * is 0x8000000000000000 rather than zero. */
    v = sf_from_decimal("1", 1, 0, SF_X87_80, &st);
    sf_to_bits(v, SF_X87_80, b);
    T_ASSERT_EQ_INT(t, b[7], 0x80); /* top of the explicit significand */
    T_ASSERT_EQ_INT(t, b[8], 0xFF); /* biased exponent 0x3FFF, low byte */
    T_ASSERT_EQ_INT(t, b[9], 0x3F);

    /* 1.0 in binary128: implicit leading one, biased exponent 0x3FFF. */
    memset(&st, 0, sizeof(st));
    v = sf_from_decimal("1", 1, 0, SF_BINARY128, &st);
    sf_to_bits(v, SF_BINARY128, b);
    T_ASSERT_EQ_INT(t, b[15], 0x3F);
    T_ASSERT_EQ_INT(t, b[14], 0xFF);
    T_ASSERT_EQ_INT(t, b[0], 0x00);

    /* 2.0 differs from 1.0 only in the exponent. */
    memset(&st, 0, sizeof(st));
    v = sf_from_decimal("2", 1, 0, SF_BINARY128, &st);
    sf_to_bits(v, SF_BINARY128, b);
    T_ASSERT_EQ_INT(t, b[15], 0x40);
    T_ASSERT_EQ_INT(t, b[14], 0x00);

    /* binary128 carries far more precision than binary64, so a value that
     * is inexact as a double is still inexact here but at a different
     * place — the point is that it does NOT round through 64 bits. */
    memset(&st, 0, sizeof(st));
    v = sf_from_decimal("1", 1, -1, SF_BINARY128, &st);
    sf_to_bits(v, SF_BINARY128, b);
    /* 0.1 in binary128 is 0x3FFB999999999999999999999999999A. */
    T_ASSERT_EQ_INT(t, b[15], 0x3F);
    T_ASSERT_EQ_INT(t, b[14], 0xFB);
    T_ASSERT_EQ_INT(t, b[0], 0x9A);
    T_ASSERT_EQ_INT(t, b[1], 0x99);
}

/* --- hex floats ---------------------------------------------------------- */

void test_softfp_hex(TestCtx *t)
{
    SfStatus st;
    struct {
        const char *digits;
        int32_t bexp;
        uint64_t want;
        const char *why;
    } rows[] = {
        {"1", 0, 0x3FF0000000000000ull, "0x1p0"},
        {"1", -1, 0x3FE0000000000000ull, "0x1p-1"},
        {"1", 1, 0x4000000000000000ull, "0x1p1"},
        {"18", -4, 0x3FF8000000000000ull, "0x1.8p0 == 1.5"},
        {"1", -1074, 0x0000000000000001ull, "0x1p-1074, min subnormal"},
        {"1", -1022, 0x0010000000000000ull, "0x1p-1022, DBL_MIN"},
        {"0", 0, 0x0000000000000000ull, "zero"},
    };
    size_t i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        uint64_t got;

        memset(&st, 0, sizeof(st));
        got = bits64(sf_from_hex(rows[i].digits, strlen(rows[i].digits),
                                 rows[i].bexp, SF_BINARY64, &st));
        if (got != rows[i].want)
            t_fail(t, __FILE__, __LINE__,
                   "hex %s p%d: 0x%016llX, want 0x%016llX (%s)", rows[i].digits,
                   (int)rows[i].bexp, (unsigned long long)got,
                   (unsigned long long)rows[i].want, rows[i].why);
        t->assertions++;
    }

    /* 0x1.fffffffffffffp1023 is DBL_MAX, exactly. */
    memset(&st, 0, sizeof(st));
    T_ASSERT(t, bits64(sf_from_hex("1fffffffffffff", 14, 1023 - 52, SF_BINARY64,
                                   &st)) == 0x7FEFFFFFFFFFFFFFull);
}

/* --- arithmetic ---------------------------------------------------------- */

static void arith_is(TestCtx *t, const char *op, Sf got, uint64_t want,
                     const char *why)
{
    uint64_t g = bits64(got);

    if (g != want)
        t_fail(t, __FILE__, __LINE__, "%s: 0x%016llX, want 0x%016llX (%s)", op,
               (unsigned long long)g, (unsigned long long)want, why);
    t->assertions++;
}

void test_softfp_arithmetic(TestCtx *t)
{
    SfStatus st;
    Sf one, two, half, zero, three;

    memset(&st, 0, sizeof(st));
    one = d64("1", 0);
    two = d64("2", 0);
    half = d64("5", -1);
    zero = d64("0", 0);
    three = d64("3", 0);

    arith_is(t, "1+1", sf_add(one, one, SF_BINARY64, &st),
             0x4000000000000000ull, "2.0");
    arith_is(t, "1+2", sf_add(one, two, SF_BINARY64, &st),
             0x4008000000000000ull, "3.0");
    arith_is(t, "2-1", sf_sub(two, one, SF_BINARY64, &st),
             0x3FF0000000000000ull, "1.0");
    arith_is(t, "1-1", sf_sub(one, one, SF_BINARY64, &st),
             0x0000000000000000ull, "exact cancellation gives +0");
    arith_is(t, "1+0", sf_add(one, zero, SF_BINARY64, &st),
             0x3FF0000000000000ull, "adding zero is identity");
    arith_is(t, "1+0.5", sf_add(one, half, SF_BINARY64, &st),
             0x3FF8000000000000ull, "1.5");
    arith_is(t, "0.5+0.5", sf_add(half, half, SF_BINARY64, &st),
             0x3FF0000000000000ull, "1.0");

    arith_is(t, "2*3", sf_mul(two, three, SF_BINARY64, &st),
             0x4018000000000000ull, "6.0");
    arith_is(t, "1*1", sf_mul(one, one, SF_BINARY64, &st),
             0x3FF0000000000000ull, "1.0");
    arith_is(t, "2*0.5", sf_mul(two, half, SF_BINARY64, &st),
             0x3FF0000000000000ull, "1.0");
    arith_is(t, "0*1", sf_mul(zero, one, SF_BINARY64, &st),
             0x0000000000000000ull, "zero");
    arith_is(t, "3*3", sf_mul(three, three, SF_BINARY64, &st),
             0x4022000000000000ull, "9.0");

    arith_is(t, "2/1", sf_div(two, one, SF_BINARY64, &st),
             0x4000000000000000ull, "2.0");
    arith_is(t, "1/2", sf_div(one, two, SF_BINARY64, &st),
             0x3FE0000000000000ull, "0.5");
    arith_is(t, "3/2", sf_div(three, two, SF_BINARY64, &st),
             0x3FF8000000000000ull, "1.5");
    /* 1/3 is the classic non-terminating case: the correctly rounded
     * result is the same pattern the decimal literal gives. */
    arith_is(t, "1/3", sf_div(one, three, SF_BINARY64, &st),
             0x3FD5555555555555ull, "1/3 correctly rounded");

    /* Sign handling. */
    arith_is(t, "-1+1", sf_add(sf_neg(one), one, SF_BINARY64, &st),
             0x0000000000000000ull, "exact cancellation");
    arith_is(t, "1*-1", sf_mul(one, sf_neg(one), SF_BINARY64, &st),
             0xBFF0000000000000ull, "-1.0");
    arith_is(t, "-1*-1", sf_mul(sf_neg(one), sf_neg(one), SF_BINARY64, &st),
             0x3FF0000000000000ull, "1.0");
}

void test_softfp_special_values(TestCtx *t)
{
    SfStatus st;
    Sf one = d64("1", 0);
    Sf zero = d64("0", 0);
    Sf inf;
    Sf nan;

    /* Division by zero yields infinity and flags invalid — the CALLER
     * decides whether that is an error, because it is one in a required
     * constant context and merely a fold failure elsewhere. */
    memset(&st, 0, sizeof(st));
    inf = sf_div(one, zero, SF_BINARY64, &st);
    T_ASSERT_EQ_INT(t, (int)inf.cls, (int)SF_INF);
    T_ASSERT(t, st.invalid);
    T_ASSERT(t, bits64(inf) == 0x7FF0000000000000ull);

    memset(&st, 0, sizeof(st));
    nan = sf_div(zero, zero, SF_BINARY64, &st);
    T_ASSERT_EQ_INT(t, (int)nan.cls, (int)SF_NAN);
    T_ASSERT(t, st.invalid);

    /* inf - inf and inf * 0 are invalid. */
    memset(&st, 0, sizeof(st));
    T_ASSERT_EQ_INT(t, (int)sf_sub(inf, inf, SF_BINARY64, &st).cls,
                    (int)SF_NAN);
    memset(&st, 0, sizeof(st));
    T_ASSERT_EQ_INT(t, (int)sf_mul(inf, zero, SF_BINARY64, &st).cls,
                    (int)SF_NAN);

    /* Infinity absorbs finite operands. */
    memset(&st, 0, sizeof(st));
    T_ASSERT_EQ_INT(t, (int)sf_add(inf, one, SF_BINARY64, &st).cls,
                    (int)SF_INF);
    memset(&st, 0, sizeof(st));
    T_ASSERT_EQ_INT(t, (int)sf_div(one, inf, SF_BINARY64, &st).cls,
                    (int)SF_ZERO);

    /* Overflow of finite operands reaches infinity and says so. */
    memset(&st, 0, sizeof(st));
    {
        Sf big = d64("17976931348623157", 292); /* DBL_MAX */
        Sf sum = sf_mul(big, d64("2", 0), SF_BINARY64, &st);

        T_ASSERT_EQ_INT(t, (int)sum.cls, (int)SF_INF);
        T_ASSERT(t, st.overflow);
    }

    /* A decimal literal past the range overflows rather than wrapping. */
    memset(&st, 0, sizeof(st));
    {
        Sf v = sf_from_decimal("1", 1, 999, SF_BINARY64, &st);

        T_ASSERT_EQ_INT(t, (int)v.cls, (int)SF_INF);
        T_ASSERT(t, st.overflow);
    }
    /* ...and one below the range underflows to zero. */
    memset(&st, 0, sizeof(st));
    {
        Sf v = sf_from_decimal("1", 1, -999, SF_BINARY64, &st);

        T_ASSERT_EQ_INT(t, (int)v.cls, (int)SF_ZERO);
    }
}

void test_softfp_compare(TestCtx *t)
{
    bool unordered;
    SfStatus st;
    Sf one = d64("1", 0);
    Sf two = d64("2", 0);
    Sf zero = d64("0", 0);
    Sf nan;

    memset(&st, 0, sizeof(st));
    nan = sf_div(zero, zero, SF_BINARY64, &st);

    T_ASSERT(t, sf_cmp(one, two, &unordered) < 0);
    T_ASSERT(t, !unordered);
    T_ASSERT(t, sf_cmp(two, one, &unordered) > 0);
    T_ASSERT(t, sf_cmp(one, one, &unordered) == 0);
    T_ASSERT(t, sf_cmp(sf_neg(one), one, &unordered) < 0);
    T_ASSERT(t, sf_cmp(sf_neg(two), sf_neg(one), &unordered) < 0);
    T_ASSERT(t, sf_cmp(zero, one, &unordered) < 0);
    T_ASSERT(t, sf_cmp(sf_neg(one), zero, &unordered) < 0);
    /* +0 and -0 compare EQUAL even though their bit patterns differ. */
    T_ASSERT(t, sf_cmp(zero, sf_neg(zero), &unordered) == 0);
    /* Anything involving a NaN is UNORDERED, which is not the same as
     * "not equal". */
    (void)sf_cmp(nan, one, &unordered);
    T_ASSERT(t, unordered);
    (void)sf_cmp(nan, nan, &unordered);
    T_ASSERT(t, unordered);
}

/* Round-tripping through the bit image is what Sprint 19 will do when it
 * writes .data and reads it back. */
void test_softfp_bit_roundtrip(TestCtx *t)
{
    static const char *const digits[] = {
        "1", "2", "5", "3141592653589793", "5", "17976931348623157", "1", "0"};
    static const int32_t exps[] = {0, 0, -1, -15, -324, 292, -1, 0};
    size_t i;

    for (i = 0; i < sizeof(exps) / sizeof(exps[0]); i++) {
        Sf v = d64(digits[i], exps[i]);
        uint8_t b[16];
        Sf back;

        sf_to_bits(v, SF_BINARY64, b);
        back = sf_from_bits(b, SF_BINARY64);
        if (bits64(back) != bits64(v))
            t_fail(t, __FILE__, __LINE__,
                   "roundtrip %se%d: 0x%016llX -> 0x%016llX", digits[i],
                   (int)exps[i], (unsigned long long)bits64(v),
                   (unsigned long long)bits64(back));
        t->assertions++;
    }
}

void test_softfp_int_conversions(TestCtx *t)
{
    SfStatus st;
    Sf v;

    /* Integer to float and back. */
    memset(&st, 0, sizeof(st));
    v = sf_from_int(42, false, SF_BINARY64, &st);
    T_ASSERT(t, bits64(v) == 0x4045000000000000ull);
    T_ASSERT_EQ_INT(t, (int)sf_to_int(v, 32, false, &st), 42);

    memset(&st, 0, sizeof(st));
    v = sf_from_int(0, false, SF_BINARY64, &st);
    T_ASSERT_EQ_INT(t, (int)v.cls, (int)SF_ZERO);
    T_ASSERT_EQ_INT(t, (int)sf_to_int(v, 32, false, &st), 0);

    /* C truncates TOWARD ZERO rather than rounding (6.3.1.4), so 3.9
     * becomes 3 and -3.9 becomes -3. */
    memset(&st, 0, sizeof(st));
    T_ASSERT_EQ_INT(t, (int)sf_to_int(d64("39", -1), 32, false, &st), 3);
    memset(&st, 0, sizeof(st));
    T_ASSERT_EQ_INT(t, (int)sf_to_int(d64("999", -3), 32, false, &st), 0);
    memset(&st, 0, sizeof(st));
    T_ASSERT_EQ_INT(t, (int)sf_to_int(d64("5", 0), 32, false, &st), 5);

    /* A value too large for the width is out of range, not wrapped. */
    memset(&st, 0, sizeof(st));
    (void)sf_to_int(d64("1", 30), 32, false, &st);
    T_ASSERT(t, st.invalid);
}

/* Converting between formats must round ONCE. Widening is exact;
 * narrowing rounds, and narrowing a value that was already narrow is the
 * identity — which is the property double rounding breaks. */
void test_softfp_format_conversion(TestCtx *t)
{
    SfStatus st;
    Sf d, f;
    uint8_t b[16];

    memset(&st, 0, sizeof(st));
    d = d64("1", -1); /* 0.1 as a double */
    f = sf_convert(d, SF_BINARY64, SF_BINARY32, &st);
    T_ASSERT(t, bits32(f) == 0x3DCCCCCDu);

    /* Widening back does NOT recover the original: the information is
     * gone, and pretending otherwise is the classic bug. */
    memset(&st, 0, sizeof(st));
    d = sf_convert(f, SF_BINARY32, SF_BINARY64, &st);
    T_ASSERT(t, bits64(d) != bits64(d64("1", -1)));

    /* An exactly representable value survives both directions. */
    memset(&st, 0, sizeof(st));
    d = d64("5", -1); /* 0.5 */
    f = sf_convert(d, SF_BINARY64, SF_BINARY32, &st);
    T_ASSERT(t, bits32(f) == 0x3F000000u);
    memset(&st, 0, sizeof(st));
    T_ASSERT(t, bits64(sf_convert(f, SF_BINARY32, SF_BINARY64, &st)) ==
                    0x3FE0000000000000ull);

    /* Widening a double into binary128 and back is exact. */
    memset(&st, 0, sizeof(st));
    {
        Sf wide = sf_convert(d64("1", -1), SF_BINARY64, SF_BINARY128, &st);
        Sf narrow;

        memset(&st, 0, sizeof(st));
        narrow = sf_convert(wide, SF_BINARY128, SF_BINARY64, &st);
        T_ASSERT(t, bits64(narrow) == bits64(d64("1", -1)));
    }

    /* Overflow when narrowing: DBL_MAX has no binary32 representation. */
    memset(&st, 0, sizeof(st));
    f = sf_convert(d64("17976931348623157", 292), SF_BINARY64, SF_BINARY32,
                   &st);
    T_ASSERT_EQ_INT(t, (int)f.cls, (int)SF_INF);
    T_ASSERT(t, st.overflow);
    sf_to_bits(f, SF_BINARY32, b);
    T_ASSERT_EQ_INT(t, b[3], 0x7F);
}

/* The bignum's own edges. A truncated bignum is a WRONG constant, so
 * refusing is the only acceptable failure mode. */
void test_softfp_extreme_inputs(TestCtx *t)
{
    SfStatus st;
    char many[600];
    unsigned i;

    /* A long digit string that still names a modest value. */
    memset(many, '9', sizeof(many));
    memset(&st, 0, sizeof(st));
    {
        Sf v = sf_from_decimal(many, sizeof(many), -600, SF_BINARY64, &st);

        /* 0.999...9 with 600 nines rounds to exactly 1.0. */
        T_ASSERT(t, bits64(v) == 0x3FF0000000000000ull);
    }

    /* Leading zeros must not inflate the bignum or change the value. */
    memset(&st, 0, sizeof(st));
    T_ASSERT(t, bits64(sf_from_decimal("0000000001", 10, 0, SF_BINARY64,
                                       &st)) == 0x3FF0000000000000ull);

    /* Every digit position of a 40-digit mantissa participates. */
    for (i = 1; i <= 20; i++) {
        char buf[64];
        Sf a, b;
        unsigned k;

        for (k = 0; k < 20; k++)
            buf[k] = '1';
        buf[20] = '\0';
        memset(&st, 0, sizeof(st));
        a = sf_from_decimal(buf, 20, -20, SF_BINARY64, &st);
        buf[i - 1] = '2';
        memset(&st, 0, sizeof(st));
        b = sf_from_decimal(buf, 20, -20, SF_BINARY64, &st);
        /* Changing any of the first 17 digits must change the double;
         * beyond that the change can fall below the ulp. */
        if (i <= 16 && bits64(a) == bits64(b))
            t_fail(t, __FILE__, __LINE__, "digit %u did not affect the result",
                   i);
        t->assertions++;
    }
}
