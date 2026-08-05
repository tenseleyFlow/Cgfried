/* Exercises every binary128 soft-float entry point and prints raw bits.
 *
 * The same probe is linked twice — once against libgcc, once against
 * libcgf_rt — and the two outputs must be byte-identical. That makes libgcc
 * the oracle for both the VALUES and the calling contract, which is the same
 * technique Sprint 28 used for the 128-bit integer routines.
 *
 * Every operand is volatile so nothing folds at compile time: a folded
 * expression would produce identical output from both links while testing
 * neither implementation. */

#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned long long lo, hi;
} Bits;

static long double from_bits(unsigned long long hi, unsigned long long lo)
{
    unsigned char b[16];
    long double v;

    memcpy(b, &lo, 8);
    memcpy(b + 8, &hi, 8);
    memcpy(&v, b, 16);
    return v;
}

static Bits to_bits(long double v)
{
    unsigned char b[16];
    Bits r;

    memcpy(b, &v, 16);
    memcpy(&r.lo, b, 8);
    memcpy(&r.hi, b + 8, 8);
    return r;
}

static void show(const char *tag, long double v)
{
    Bits b = to_bits(v);

    printf("%s %016llx%016llx\n", tag, b.hi, b.lo);
}

/* hi:lo of interesting binary128 patterns: zeroes, one, powers, the format
 * boundaries, subnormals, and the non-finite corners. */
static const Bits VALUES[] = {
    {0x0000000000000000ULL, 0x0000000000000000ULL}, /* +0 */
    {0x0000000000000000ULL, 0x8000000000000000ULL}, /* -0 */
    {0x0000000000000000ULL, 0x3fff000000000000ULL}, /* 1 */
    {0x0000000000000000ULL, 0xbfff000000000000ULL}, /* -1 */
    {0x0000000000000000ULL, 0x4000000000000000ULL}, /* 2 */
    {0x0000000000000000ULL, 0x3ffe000000000000ULL}, /* 0.5 */
    {0x3333333333333333ULL, 0x3ffb999999999999ULL}, /* ~0.1 */
    {0x0000000000000000ULL, 0x4005f80000000000ULL}, /* 126 */
    {0xffffffffffffffffULL, 0x7ffeffffffffffffULL}, /* max finite */
    {0x0000000000000000ULL, 0x0001000000000000ULL}, /* min normal */
    {0x0000000000000001ULL, 0x0000000000000000ULL}, /* min subnormal */
    {0xffffffffffffffffULL, 0x0000ffffffffffffULL}, /* max subnormal */
    {0x0000000000000000ULL, 0x7fff000000000000ULL}, /* +inf */
    {0x0000000000000000ULL, 0xffff000000000000ULL}, /* -inf */
    {0x0000000000000000ULL, 0x7fff800000000000ULL}, /* quiet NaN */
    {0x0000000000000000ULL, 0x4009210fdaa22168ULL}, /* ~pi*2^6 */
};
#define NVALUES ((int)(sizeof(VALUES) / sizeof(VALUES[0])))

static const long long INTS[] = {0,
                                 1,
                                 -1,
                                 2,
                                 -2,
                                 127,
                                 -128,
                                 65535,
                                 -65536,
                                 2147483647LL,
                                 -2147483648LL,
                                 4294967295LL,
                                 9007199254740993LL,
                                 -9007199254740993LL,
                                 9223372036854775807LL,
                                 -9223372036854775807LL - 1};
#define NINTS ((int)(sizeof(INTS) / sizeof(INTS[0])))

static const double DOUBLES[] = {0.0,      -0.0, 1.0,   -1.0,
                                 0.5,      0.1,  1e308, 1e-308,
                                 3.0e-320, 1.5,  -2.25, 123456789.0};
#define NDOUBLES ((int)(sizeof(DOUBLES) / sizeof(DOUBLES[0])))

int main(void)
{
    int i, j;

    for (i = 0; i < NVALUES; i++) {
        for (j = 0; j < NVALUES; j++) {
            volatile long double a = from_bits(VALUES[i].hi, VALUES[i].lo);
            volatile long double b = from_bits(VALUES[j].hi, VALUES[j].lo);

            show("add", a + b);
            show("sub", a - b);
            show("mul", a * b);
            show("div", a / b);
            /* The comparison results, not the raw libcall returns: this is
             * what the contract actually has to deliver, including every
             * NaN-unordered case answering false. */
            printf("cmp %d%d%d%d%d%d\n", a == b, a != b, a<b, a <= b, a> b,
                   a >= b);
        }
    }

    /* Negation is a bit operation, so the NaN and -0.0 rows are the whole
     * test: `0 - x` would agree on every other value in the table. */
    for (i = 0; i < NVALUES; i++) {
        volatile long double a = from_bits(VALUES[i].hi, VALUES[i].lo);

        show("neg", -a);
        show("negneg", -(-a));
    }

    for (i = 0; i < NVALUES; i++) {
        volatile long double a = from_bits(VALUES[i].hi, VALUES[i].lo);
        volatile double d;
        volatile float f;

        d = (double)a;
        f = (float)a;
        printf("trunc %016llx %08lx\n",
               (unsigned long long)(*(volatile unsigned long long *)&d),
               (unsigned long)(*(volatile unsigned int *)&f));
        printf("fix %d %lld %u %llu\n", (int)a, (long long)a, (unsigned)a,
               (unsigned long long)a);
    }

    for (i = 0; i < NINTS; i++) {
        volatile long long v = INTS[i];
        volatile unsigned long long u = (unsigned long long)INTS[i];

        show("floatdi", (long double)v);
        show("floatsi", (long double)(int)v);
        show("floatundi", (long double)u);
        show("floatunsi", (long double)(unsigned int)u);
    }

    for (i = 0; i < NDOUBLES; i++) {
        volatile double d = DOUBLES[i];
        volatile float f = (float)DOUBLES[i];

        show("extdf", (long double)d);
        show("extsf", (long double)f);
    }
    return 0;
}
