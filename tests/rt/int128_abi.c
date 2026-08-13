/* Host-oracle ABI test for src/rt/int128.c.
 *
 * Build once with REFERENCE to use the host compiler's scalar __int128
 * operations, then once with src/rt/int128.c linked first.  The latter calls
 * the libgcc-compatible symbols explicitly, so equality proves both the
 * arithmetic and the scalar-to-two-limb ABI boundary. */
#include <stdint.h>
#include <stdio.h>

typedef unsigned __int128 u128;
typedef __int128 i128;

#ifndef REFERENCE
extern u128 __udivti3(u128, u128);
extern u128 __umodti3(u128, u128);
extern i128 __divti3(i128, i128);
extern i128 __modti3(i128, i128);
extern u128 __multi3(u128, u128);
extern i128 __ashlti3(i128, int);
extern i128 __ashrti3(i128, int);
extern u128 __lshrti3(u128, int);
extern int __popcountdi2(uint64_t);
extern int __clzdi2(uint64_t);
extern int __ctzdi2(uint64_t);
#endif

static uint64_t state = UINT64_C(0x58c0ffee12345678);

static uint64_t next64(void)
{
    uint64_t x = state;

    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state = x;
    return x * UINT64_C(2685821657736338717);
}

static u128 make128(uint64_t lo, uint64_t hi)
{
    return ((u128)hi << 64) | lo;
}

static uint64_t mix(uint64_t h, u128 v)
{
    h ^= (uint64_t)v;
    h *= UINT64_C(1099511628211);
    h ^= (uint64_t)(v >> 64);
    return h * UINT64_C(1099511628211);
}

#ifdef REFERENCE
static int pop_ref(uint64_t x)
{
    int n = 0;

    while (x) {
        x &= x - 1;
        n++;
    }
    return n;
}

static int clz_ref(uint64_t x)
{
    int n = 0;

    while (!(x & (UINT64_C(1) << 63))) {
        x <<= 1;
        n++;
    }
    return n;
}

static int ctz_ref(uint64_t x)
{
    int n = 0;

    while (!(x & 1)) {
        x >>= 1;
        n++;
    }
    return n;
}
#endif

int main(void)
{
    uint64_t h[9];
    unsigned i;

    for (i = 0; i < 9; i++)
        h[i] = UINT64_C(1469598103934665603);

    for (i = 0; i < 4096; i++) {
        uint64_t alo = next64();
        uint64_t ahi = next64();
        uint64_t blo = next64();
        uint64_t bhi = next64();
        u128 a = make128(alo, ahi);
        u128 b = make128(blo, bhi) | 1;
        i128 sa = (i128)a;
        i128 sb = (i128)b;
        int shift = (int)(i & 127);
        uint64_t bits = next64() | 1;
        u128 uq, ur, product, left, logical;
        i128 sq, sr, arithmetic;
        int pop, clz, ctz;

        /* INT128_MIN / -1 is undefined in C and intentionally omitted. */
        if (sa == (i128)((u128)1 << 127) && sb == -1)
            sb = 1;

#ifdef REFERENCE
        uq = a / b;
        ur = a % b;
        sq = sa / sb;
        sr = sa % sb;
        product = a * b;
        left = (u128)sa << shift;
        arithmetic = sa >> shift;
        logical = a >> shift;
        pop = pop_ref(bits);
        clz = clz_ref(bits);
        ctz = ctz_ref(bits);
#else
        uq = __udivti3(a, b);
        ur = __umodti3(a, b);
        sq = __divti3(sa, sb);
        sr = __modti3(sa, sb);
        product = __multi3(a, b);
        left = (u128)__ashlti3(sa, shift);
        arithmetic = __ashrti3(sa, shift);
        logical = __lshrti3(a, shift);
        pop = __popcountdi2(bits);
        clz = __clzdi2(bits);
        ctz = __ctzdi2(bits);
#endif
        h[0] = mix(h[0], uq);
        h[1] = mix(h[1], ur);
        h[2] = mix(h[2], (u128)sq);
        h[3] = mix(h[3], (u128)sr);
        h[4] = mix(h[4], product);
        h[5] = mix(h[5], left);
        h[6] = mix(h[6], (u128)arithmetic);
        h[7] = mix(h[7], logical);
        h[8] ^= (uint64_t)(pop * 131 + clz * 17 + ctz);
    }
    for (i = 0; i < 9; i++)
        printf("%016llx%c", (unsigned long long)h[i], i == 8 ? '\n' : ' ');
    return 0;
}
