/* The vectorizer emits target-neutral IR, so what is under test here is the
 * NEON lowering: arrangement specifiers, the whole-q load/store, and the
 * ext/op halving that stands in for the horizontal reductions NEON does not
 * have for most operations. */
#include <stdio.h>
#include <string.h>

int sum4(const int *p);
int xor4(const int *p);
long sum2(const long *p);
void addvec(const int *a, const int *b, int *out);
void splat(int *out, int v);
int lane2(const int *p);

static int fails;
static void chk(const char *w, long g, long want)
{ if (g != want) { printf("FAIL %s: %ld vs %ld\n", w, g, want); fails++; } }

int main(void)
{
    static const int rows[][4] = {
        {0, 0, 0, 0},        {1, 2, 3, 4},
        {-1, -2, -3, -4},    {2147483647, 1, 0, 0},
        {-2147483648, -1, 0, 0}, {65536, 65536, 65536, 65536},
        {0x0f0f0f0f, 0x33333333, 0x55555555, 0xffff},
    };
    unsigned r;

    for (r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        _Alignas(16) int a[4], b[4], out[4];
        long w[2];
        unsigned s = 0, x = 0;
        int i;

        memcpy(a, rows[r], sizeof(a));
        for (i = 0; i < 4; i++) {
            b[i] = rows[r][3 - i];
            s += (unsigned)a[i];
            x ^= (unsigned)a[i];
        }
        chk("sum4", sum4(a), (int)s);
        chk("xor4", xor4(a), (int)x);
        chk("lane2", lane2(a), a[2]);

        addvec(a, b, out);
        for (i = 0; i < 4; i++)
            chk("addvec", out[i], (int)((unsigned)a[i] + (unsigned)b[i]));

        splat(out, a[1]);
        for (i = 0; i < 4; i++)
            chk("splat", out[i], a[1]);

        w[0] = (long)a[0] << 20;
        w[1] = (long)a[1] << 20;
        chk("sum2", sum2(w), w[0] + w[1]);
    }
    printf(fails ? "FAILURES %d\n" : "OK\n", fails);
    return fails != 0;
}
