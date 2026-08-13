/* libcgf_rt: 128-bit integer support, with LIBGCC-COMPATIBLE NAMES.
 *
 * Cgfried deliberately does not implement the GNU __int128 source type.  The
 * supported little-endian x86-64 SysV and AAPCS64 ABIs nevertheless pass and
 * return a scalar 128-bit integer in the same two registers as this two-u64
 * aggregate (low limb first).  That lets this strict-C11 implementation keep
 * the libgcc symbol ABI without requiring the extension it exists to support.
 *
 * None of these routines may use an operation that lowers to itself. */

typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct {
    u64 lo;
    u64 hi;
} U128;

_Static_assert(sizeof(u32) == 4, "int128 runtime requires 32-bit unsigned int");
_Static_assert(sizeof(u64) == 8,
               "int128 runtime requires 64-bit unsigned long long");
_Static_assert(sizeof(U128) == 16,
               "int128 runtime ABI requires a 16-byte pair");

static U128 pair(u64 lo, u64 hi)
{
    U128 v;

    v.lo = lo;
    v.hi = hi;
    return v;
}

static int pair_is_zero(U128 a)
{
    return a.lo == 0 && a.hi == 0;
}

static int pair_cmp(U128 a, U128 b)
{
    if (a.hi != b.hi)
        return a.hi < b.hi ? -1 : 1;
    if (a.lo != b.lo)
        return a.lo < b.lo ? -1 : 1;
    return 0;
}

static U128 pair_sub(U128 a, U128 b)
{
    U128 r;

    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo);
    return r;
}

static U128 pair_neg(U128 a)
{
    U128 r;

    r.lo = ~a.lo + 1;
    r.hi = ~a.hi + (r.lo == 0);
    return r;
}

static U128 pair_shl1(U128 a)
{
    return pair(a.lo << 1, (a.hi << 1) | (a.lo >> 63));
}

static unsigned pair_bit(U128 a, int bit)
{
    if (bit >= 64)
        return (unsigned)((a.hi >> (bit - 64)) & 1);
    return (unsigned)((a.lo >> bit) & 1);
}

static void pair_set_bit(U128 *a, int bit)
{
    if (bit >= 64)
        a->hi |= 1ULL << (bit - 64);
    else
        a->lo |= 1ULL << bit;
}

/* Restoring long division.  The carry records the 129th bit of the running
 * remainder; discarding it before the comparison is a subtle bug when the
 * divisor has its top bit set. */
static U128 udiv128(U128 n, U128 d, U128 *rem)
{
    U128 q = {0, 0};
    U128 r = {0, 0};
    int i;

    if (pair_is_zero(d)) {
        volatile u64 zero = 0;

        if (rem)
            *rem = pair(0, 0);
        return pair(1 / zero, 0);
    }

    if (d.hi == 0 && n.hi == 0) {
        if (rem)
            *rem = pair(n.lo % d.lo, 0);
        return pair(n.lo / d.lo, 0);
    }

    for (i = 127; i >= 0; i--) {
        unsigned carry = (unsigned)(r.hi >> 63);

        r = pair_shl1(r);
        r.lo |= pair_bit(n, i);
        if (carry || pair_cmp(r, d) >= 0) {
            r = pair_sub(r, d);
            pair_set_bit(&q, i);
        }
    }
    if (rem)
        *rem = r;
    return q;
}

U128 __udivti3(U128 a, U128 b)
{
    return udiv128(a, b, 0);
}

U128 __umodti3(U128 a, U128 b)
{
    U128 r;

    (void)udiv128(a, b, &r);
    return r;
}

/* Signed quotient truncates toward zero; remainder takes the dividend sign.
 * Two's-complement magnitude conversion also handles INT128_MIN. */
U128 __divti3(U128 a, U128 b)
{
    int neg = (int)((a.hi ^ b.hi) >> 63);
    U128 ua = a.hi >> 63 ? pair_neg(a) : a;
    U128 ub = b.hi >> 63 ? pair_neg(b) : b;
    U128 q = udiv128(ua, ub, 0);

    return neg ? pair_neg(q) : q;
}

U128 __modti3(U128 a, U128 b)
{
    int neg = (int)(a.hi >> 63);
    U128 ua = neg ? pair_neg(a) : a;
    U128 ub = b.hi >> 63 ? pair_neg(b) : b;
    U128 r;

    (void)udiv128(ua, ub, &r);
    return neg ? pair_neg(r) : r;
}

/* Exact 64x64 -> 128 multiplication using four 32-bit partial products. */
static U128 mul64(u64 a, u64 b)
{
    const u64 mask = 0xffffffffULL;
    u64 a0 = (u32)a;
    u64 a1 = a >> 32;
    u64 b0 = (u32)b;
    u64 b1 = b >> 32;
    u64 t = a0 * b0;
    u64 w0 = t & mask;
    u64 k = t >> 32;
    u64 w1, w2;

    t = a1 * b0 + k;
    w1 = t & mask;
    w2 = t >> 32;
    t = a0 * b1 + w1;
    k = t >> 32;
    return pair((t << 32) + w0, a1 * b1 + w2 + k);
}

U128 __multi3(U128 a, U128 b)
{
    U128 lo = mul64(a.lo, b.lo);

    lo.hi += a.hi * b.lo + a.lo * b.hi;
    return lo;
}

U128 __ashlti3(U128 a, int b)
{
    if (b <= 0)
        return a;
    if (b < 64)
        return pair(a.lo << b, (a.hi << b) | (a.lo >> (64 - b)));
    if (b < 128)
        return pair(0, a.lo << (b - 64));
    return pair(0, 0);
}

U128 __lshrti3(U128 a, int b)
{
    if (b <= 0)
        return a;
    if (b < 64)
        return pair((a.lo >> b) | (a.hi << (64 - b)), a.hi >> b);
    if (b < 128)
        return pair(a.hi >> (b - 64), 0);
    return pair(0, 0);
}

U128 __ashrti3(U128 a, int b)
{
    u64 fill = a.hi >> 63 ? ~0ULL : 0;

    if (b <= 0)
        return a;
    if (b < 64)
        return pair((a.lo >> b) | (a.hi << (64 - b)),
                    (a.hi >> b) | (fill << (64 - b)));
    if (b == 64)
        return pair(a.hi, fill);
    if (b < 128)
        return pair((a.hi >> (b - 64)) | (fill << (128 - b)), fill);
    return pair(fill, fill);
}

int __popcountdi2(u64 a)
{
    int n = 0;

    while (a) {
        a &= a - 1;
        n++;
    }
    return n;
}

/* As in libgcc, clz/ctz are undefined for zero. */
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
