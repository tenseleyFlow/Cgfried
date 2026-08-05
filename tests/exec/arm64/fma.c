/* The point of contraction is ONE rounding step, so the fused result must
 * equal libm's fma() and must differ from the two-step form on inputs where
 * the intermediate product is inexact. */
#include <math.h>
#include <stdio.h>

double fused(double a, double b, double c);
double unfused(double a, double b, double c);
double fused_sub(double a, double b, double c);

static int fails, differed;
static void chk(const char *w, double g, double want)
{
    if (g != want) {
        printf("FAIL %s: %.20g vs %.20g\n", w, g, want);
        fails++;
    }
}

int main(void)
{
    static const double rows[][3] = {
        {1.0, 2.0, 3.0},
        {0.1, 0.1, -0.01},
        {1e300, 1e-300, 1.0},
        {1.0000000000000002, 1.0000000000000002, -1.0000000000000004},
        {3.0, 1.0 / 3.0, -1.0},
        {-2.5, 4.25, 7.125},
    };
    unsigned i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        double a = rows[i][0], b = rows[i][1], c = rows[i][2];

        chk("fused", fused(a, b, c), fma(a, b, c));
        {
            /* volatile, because this driver is compiled by gcc at -O2 in a
             * GNU dialect and gcc would CONTRACT the reference itself —
             * which is exactly the policy under test. Forcing the product
             * to memory keeps the two-rounding form honest. */
            volatile double product = a * b;

            chk("unfused", unfused(a, b, c), product + c);
        }
        chk("fused_sub", fused_sub(a, b, c), fma(-a, b, c));
        if (fused(a, b, c) != unfused(a, b, c))
            differed++;
    }
    /* If no row ever separated the two, the test proves nothing. */
    if (!differed) {
        printf("FAIL: fused and unfused never differed\n");
        fails++;
    }
    printf(fails ? "FAILURES %d\n" : "OK\n", fails);
    return fails != 0;
}
