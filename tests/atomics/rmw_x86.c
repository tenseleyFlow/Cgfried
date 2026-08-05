#include <stdio.h>
static int fails;
static void chk(const char *w, long g, long want)
{ if (g != want) { printf("FAIL %s: %ld vs %ld\n", w, g, want); fails++; } }

int main(void)
{
    static const int v[] = {0, 1, -1, 7, -7, 65535, -65536, 2147483647};
    unsigned i, j;

    for (i = 0; i < 8; i++)
        for (j = 0; j < 8; j++) {
            _Atomic int c;
            _Atomic long w;

            c = v[i]; c += v[j];
            chk("add", c, (int)((unsigned)v[i] + (unsigned)v[j]));
            c = v[i]; c -= v[j];
            chk("sub", c, (int)((unsigned)v[i] - (unsigned)v[j]));
            c = v[i]; c &= v[j];  chk("and", c, v[i] & v[j]);
            c = v[i]; c |= v[j];  chk("or",  c, v[i] | v[j]);
            c = v[i]; c ^= v[j];  chk("xor", c, v[i] ^ v[j]);
            w = v[i]; w += v[j];
            chk("add64", w, (long)v[i] + v[j]);
            w = v[i]; w &= v[j];  chk("and64", w, (long)v[i] & v[j]);
            /* a plain read and a plain write of an _Atomic object */
            c = v[j];
            chk("store/load", c, v[j]);
        }
    /* the float path: no fetch_add exists, so it becomes a cmpxchg loop */
    {
        _Atomic double d = 1.5;
        d += 2.25;
        chk("double", (long)(d * 100), 375);
    }
    printf(fails ? "FAILURES %d\n" : "OK\n", fails);
    return fails != 0;
}
