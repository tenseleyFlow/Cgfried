/* libcgf_rt: 128-bit integer support, with LIBGCC-COMPATIBLE NAMES.
 *
 * The names are the contract (locked decision): objects we compile must
 * mix with gcc-compiled objects, so a program half-built by each linker
 * must resolve __udivti3 to ONE implementation and behave identically
 * either way.
 *
 * THE BOOTSTRAP RULE: this file must never use 128-bit `/` or `%`.
 * When cgf compiles it (Sprint 58) those lower to calls to the very
 * functions defined here, and the recursion is infinite. Shifts,
 * compares, add and subtract lower inline, so the shift-subtract
 * long division below is safe. Do not "simplify" it back to `n / d`.
 *
 * Compiled by the HOST cc until Sprint 58 flips it (RT_CC in the
 * Makefile); the flip is part of the self-host DoD. */

typedef unsigned long long u64;
typedef unsigned __int128 u128;
typedef signed __int128 i128;

/* Unsigned 128/128 division, remainder out. The fast path handles the
 * case both operands fit in 64 bits, where the host's own 64-bit
 * division is a single instruction. */
static u128 udiv128(u128 n, u128 d, u128 *rem)
{
    u128 q = 0, r = 0;
    int i;

    if (d == 0) {
        /* Match the hardware: an integer divide by zero traps (SIGFPE
         * on x86_64 via the div instruction). Returning 0 would turn a
         * crash the programmer can find into a wrong answer they
         * cannot. The division below is 64-bit, so it is not a
         * recursive call into this function. */
        u64 zero = 0;

        if (rem)
            *rem = 0;
        return (u128)(1 / zero);
    }
    if ((d >> 64) == 0 && (n >> 64) == 0) {
        u64 n0 = (u64)n, d0 = (u64)d;

        if (rem)
            *rem = n0 % d0;
        return n0 / d0;
    }
    /* Shift-subtract, MSB first: r accumulates the running remainder
     * and q the quotient, one bit per step. Only shifts, compares and
     * subtraction — all of which lower inline. */
    for (i = 127; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= (u128)1 << i;
        }
    }
    if (rem)
        *rem = r;
    return q;
}

u128 __udivti3(u128 a, u128 b)
{
    return udiv128(a, b, 0);
}

u128 __umodti3(u128 a, u128 b)
{
    u128 r;

    udiv128(a, b, &r);
    return r;
}

/* Signed forms: C99 truncates toward zero, so the quotient's sign is
 * the XOR of the operands' and the remainder takes the DIVIDEND's. */
i128 __divti3(i128 a, i128 b)
{
    int neg = 0;
    u128 ua, ub, q;

    if (a < 0) {
        ua = (u128)0 - (u128)a;
        neg = !neg;
    } else {
        ua = (u128)a;
    }
    if (b < 0) {
        ub = (u128)0 - (u128)b;
        neg = !neg;
    } else {
        ub = (u128)b;
    }
    q = udiv128(ua, ub, 0);
    return neg ? -(i128)q : (i128)q;
}

i128 __modti3(i128 a, i128 b)
{
    int neg = a < 0;
    u128 ua, ub, r;

    ua = a < 0 ? (u128)0 - (u128)a : (u128)a;
    ub = b < 0 ? (u128)0 - (u128)b : (u128)b;
    udiv128(ua, ub, &r);
    return neg ? -(i128)r : (i128)r;
}

/* 128x128 multiply from 64-bit halves. isel usually inlines this, but
 * the symbol must exist for objects that call it. */
u128 __multi3(u128 a, u128 b)
{
    u64 al = (u64)a, ah = (u64)(a >> 64);
    u64 bl = (u64)b, bh = (u64)(b >> 64);
    /* Only the low 128 bits survive, so the ah*bh term (which starts at
     * bit 128) is dropped entirely — the same reason isel can inline
     * this as one mul plus two multiply-adds. */
    u128 lo = (u128)al * bl;
    u128 mid = (u128)ah * bl + (u128)al * bh;

    return lo + (mid << 64);
}

/* Variable-count 128-bit shifts. The count is taken mod 128 the way the
 * hardware would NOT do it — a count >= 128 is undefined in C, and
 * libgcc's own versions produce these answers. */
i128 __ashlti3(i128 a, int b)
{
    return (i128)((u128)a << b);
}

i128 __ashrti3(i128 a, int b)
{
    return a >> b;
}

u128 __lshrti3(u128 a, int b)
{
    return a >> b;
}

/* Bit counts. libgcc's __clzdi2/__ctzdi2 are UNDEFINED at zero; the
 * loops below would return 64, which is a defined answer we do not
 * promise — callers must not pass zero. */
int __popcountdi2(u64 a)
{
    int n = 0;

    while (a) {
        a &= a - 1;
        n++;
    }
    return n;
}

int __clzdi2(u64 a)
{
    int n = 0;

    while (!(a & 0x8000000000000000ULL)) {
        a <<= 1;
        n++;
    }
    return n;
}

int __ctzdi2(u64 a)
{
    int n = 0;

    while (!(a & 1)) {
        a >>= 1;
        n++;
    }
    return n;
}
