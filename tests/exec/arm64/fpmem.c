#include <stdio.h>
double farith(double a, double b);
long roundtrip(long n);
float narrow(double x);
long globals(long v);

static double farith_ref(double a, double b)
{ return ((a + b) - 1.5) * b / 2.0; }
static long roundtrip_ref(long n) { return (long)((double)n * 2.5); }
static float narrow_ref(double x) { return (float)x * 3.0f; }
static long globals_ref(long v) { return v + v * 3; }

static int fails;
static void chkd(const char *w, double got, double want)
{
    if (got != want) { printf("FAIL %s: %g vs %g\n", w, got, want); fails++; }
}
static void chk(const char *w, long got, long want)
{ if (got != want) { printf("FAIL %s: %ld vs %ld\n", w, got, want); fails++; } }

int main(void)
{
    static const double dv[] = {0.0, 1.0, -1.0, 0.5, -2.75, 1e10, -3.125};
    unsigned i, j;
    long n;

    for (i = 0; i < 7; i++) {
        for (j = 0; j < 7; j++)
            chkd("farith", farith(dv[i], dv[j]), farith_ref(dv[i], dv[j]));
        chkd("narrow", narrow(dv[i]), narrow_ref(dv[i]));
    }
    for (n = -1000; n <= 1000; n += 37)
        chk("roundtrip", roundtrip(n), roundtrip_ref(n));
    for (n = -50; n < 50; n++) chk("globals", globals(n), globals_ref(n));
    printf(fails ? "FAILURES %d\n" : "OK\n", fails);
    return fails != 0;
}
