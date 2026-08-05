#include <stdio.h>
long call10(long x);
long fact(long n);
long mixedargs(long a, double x, long b, double y);

static long call10_ref(long x)
{ return x + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 * 1000; }
static long fact_ref(long n) { return n <= 1 ? 1 : n * fact_ref(n - 1); }
static long mixedargs_ref(long a, double x, long b, double y)
{ return a + b + (long)(x + y); }

static int fails;
static void chk(const char *w, long got, long want)
{ if (got != want) { printf("FAIL %s: %ld vs %ld\n", w, got, want); fails++; } }

int main(void)
{
    long i;
    static const double dv[] = {0.0, 1.5, -2.25, 100.75, -0.5};
    unsigned a, b;

    for (i = -5; i < 20; i++) chk("call10", call10(i), call10_ref(i));
    for (i = 0; i < 21; i++) chk("fact", fact(i), fact_ref(i));
    for (a = 0; a < 5; a++)
        for (b = 0; b < 5; b++)
            chk("mixedargs", mixedargs((long)a, dv[a], (long)b * 3, dv[b]),
                mixedargs_ref((long)a, dv[a], (long)b * 3, dv[b]));
    printf(fails ? "FAILURES %d\n" : "OK\n", fails);
    return fails != 0;
}
