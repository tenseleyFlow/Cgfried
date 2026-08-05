/* The oracle is the C reference beside each call: aarch64-linux-gnu-gcc
 * compiles it, we compile the .cgfir, and the two must agree. Hard-coded
 * expected values would only prove the emitter is self-consistent. */
#include <stdio.h>

long mix(long a, long b, int c);
int divrem(int a, int b);

static long mix_ref(long a, long b, int c)
{
    return (((a + b - 7) * 3 + (long)c) << 2) & 1048575;
}

static int divrem_ref(int a, int b) { return a / b * 100 + a % b; }

static int fails;

static void chk_l(const char *what, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %ld want %ld\n", what, got, want);
        fails++;
    }
}

int main(void)
{
    static const long va[] = {0, 1, -1, 5, -7, 1000000, -1000000, 123456789};
    static const int vc[] = {0, 1, -1, 3, -3, 70000, -70000};
    unsigned i, j, k;

    for (i = 0; i < sizeof(va) / sizeof(va[0]); i++)
        for (j = 0; j < sizeof(va) / sizeof(va[0]); j++)
            for (k = 0; k < sizeof(vc) / sizeof(vc[0]); k++)
                chk_l("mix", mix(va[i], va[j], vc[k]),
                      mix_ref(va[i], va[j], vc[k]));

    for (i = 0; i < sizeof(vc) / sizeof(vc[0]); i++)
        for (j = 0; j < sizeof(vc) / sizeof(vc[0]); j++)
            if (vc[j] != 0)
                chk_l("divrem", divrem(vc[i], vc[j]),
                      divrem_ref(vc[i], vc[j]));

    printf(fails ? "FAILURES %d\n" : "OK\n", fails);
    return fails != 0;
}
