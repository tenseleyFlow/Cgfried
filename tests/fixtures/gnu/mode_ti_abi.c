/* Host-oracle differential for GNU mode(TI).
 *
 * The host compiler supplies the reference executable. Cgfried compiles the
 * same source and links it with src/rt/int128.c, so matching output proves
 * source semantics, optimization, the helper-call boundary, and scalar TI
 * parameter/return ABI agreement in one deterministic run. */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

_Static_assert(sizeof(u128) == 16, "mode(TI) must be 16 bytes");
_Static_assert(_Alignof(u128) == 16, "mode(TI) must be 16-aligned");

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

/* A u64 before the first TI argument forces AAPCS64 to skip x1 and start the
 * 16-aligned value in x2:x3; the second TI and trailing u64 pin the remainder
 * of the register assignment. SysV supplies the independent two-eightbyte
 * contract. */
static u128 abi_round(uint64_t before, u128 a, u128 b, uint64_t after)
{
    return a * b + (u128)before + (u128)after;
}

static u128 abi_var(uint64_t before, ...)
{
    va_list ap;
    u128 a;
    u128 b;
    uint64_t after;

    va_start(ap, before);
    a = va_arg(ap, u128);
    b = va_arg(ap, u128);
    after = va_arg(ap, uint64_t);
    va_end(ap);
    return a + b + (u128)before + (u128)after;
}

int main(void)
{
    uint64_t h[15];
    unsigned i;

    for (i = 0; i < 15; i++)
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
        u128 uq, ur, product, left, logical, abi;
        i128 sq, sr, arithmetic;
        u128 mutate = a;
        int relations;

        /* The sole signed-division overflow is intentionally outside the
         * oracle domain, just as ordinary C requires. */
        if (sa == (i128)((u128)1 << 127) && sb == (i128)-1)
            sb = 1;
        uq = a / b;
        ur = a % b;
        sq = sa / sb;
        sr = sa % sb;
        product = a * b;
        left = a << shift;
        arithmetic = sa >> shift;
        logical = a >> shift;
        abi = abi_round(alo, a, b, bhi);
        mutate += b;
        mutate ^= a;
        mutate -= b;
        mutate |= (u128)1;
        mutate &= ~((u128)2);
        if (i & 1)
            mutate++;
        else
            mutate--;
        relations = (a < b) | ((a <= b) << 1) | ((a > b) << 2) |
                    ((a >= b) << 3) | ((a == b) << 4) | ((a != b) << 5) |
                    ((sa < sb) << 6) | ((sa >= sb) << 7);

        h[0] = mix(h[0], uq);
        h[1] = mix(h[1], ur);
        h[2] = mix(h[2], (u128)sq);
        h[3] = mix(h[3], (u128)sr);
        h[4] = mix(h[4], product);
        h[5] = mix(h[5], left);
        h[6] = mix(h[6], (u128)arithmetic);
        h[7] = mix(h[7], logical);
        h[8] = mix(h[8], abi);
        h[9] = mix(h[9], mutate);
        h[10] = mix(h[10], (u128)(unsigned)relations);
        h[11] = mix(h[11], (u128)(uint64_t)a);
        h[12] = mix(h[12], (u128)(a ? 1 : 0));
        h[13] = mix(h[13], (u128)(!a));
        h[14] = mix(h[14], abi_var(alo, a, b, bhi));
    }
    for (i = 0; i < 15; i++)
        printf("%016llx%c", (unsigned long long)h[i], i == 14 ? '\n' : ' ');
    return 0;
}
