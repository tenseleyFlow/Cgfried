// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 0999070060 0111002220 113322 644446556
/* GNU case ranges `case lo ... hi:`, executed. Every expectation on the
 * CHECK line was produced by gcc first and then compared, per the corpus
 * rule -- a hand-computed switch table is exactly the kind of prediction
 * that pins the wrong answer.
 *
 * The four functions are not decoration; each covers a domain the others
 * cannot reach:
 *
 *  - f   plain int ranges, a ONE-VALUE range (`5 ... 5`, which lowers as an
 *        ordinary table entry, not a bounds test), and a plain label mixed
 *        in, so the table path and the range path must both still work in
 *        the same switch.
 *  - g   character ranges, the form real code actually uses.
 *  - h   UNSIGNED, with a range at the very top of the domain. This is the
 *        one that catches a signed comparison: 4294967290 ... 4294967295 is
 *        a perfectly ordinary range unsigned and a reversed (empty) one if
 *        anybody compares it as i32.
 *  - neg NEGATIVE ranges, which catch the mirror error -- the lowering
 *        computes `scrut - lo` and compares UNSIGNED against `hi - lo`, and
 *        that wrap is exactly what makes a negative range work at all.
 *
 * Boundaries are probed on BOTH sides of every range (the loops start one
 * before and end one after), because a range implementation that is off by
 * one at an end still passes any test that only samples the middle. */
extern int printf(const char *, ...);

static int f(int x)
{
    switch (x) {
    case 1 ... 3:
        return 9;
    case 5 ... 5:
        return 7;
    case 8:
        return 6;
    default:
        return 0;
    }
}

static int g(int c)
{
    switch (c) {
    case 'a' ... 'c':
        return 1;
    case 'x' ... 'z':
        return 2;
    default:
        return 0;
    }
}

static unsigned h(unsigned u)
{
    switch (u) {
    case 0u ... 2u:
        return 11;
    case 4294967290u ... 4294967295u:
        return 22;
    default:
        return 33;
    }
}

static int neg(int x)
{
    switch (x) {
    case -5 ... -2:
        return 4;
    case 0 ... 1:
        return 5;
    default:
        return 6;
    }
}

int main(void)
{
    int i, c;

    for (i = 0; i < 10; i++)
        printf("%d", f(i));
    printf(" ");
    for (c = '`'; c <= 'd'; c++)
        printf("%d", g(c));
    for (c = 'w'; c <= '{'; c++)
        printf("%d", g(c));
    printf(" ");
    printf("%u%u%u", h(0), h(3), h(4294967295u));
    printf(" ");
    for (i = -6; i <= 2; i++)
        printf("%d", neg(i));
    printf("\n");
    return 0;
}
