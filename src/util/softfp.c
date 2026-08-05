#include "util/softfp.h"

#include <string.h>

#include "util/bigint.h"

/* See softfp.h for why this exists. The short version: rounding twice is
 * not the same as rounding once, and a host strtod would round through
 * whatever the host's double happens to be.
 *
 * REPRESENTATION. A value is (-1)^sign * (hi:lo) * 2^exp where (hi:lo) is
 * a 128-bit INTEGER significand. Integer rather than fractional on
 * purpose: the implicit-bit bookkeeping that fractional forms need is
 * where most softfloat bugs live. */

const SfFormat SF_BINARY32 = {8, 23, false, 4};
const SfFormat SF_BINARY64 = {11, 52, false, 8};
/* x87 80-bit stores the leading one EXPLICITLY, which is why it needs its
 * own flag rather than a wider binary64. */
const SfFormat SF_X87_80 = {15, 64, true, 10};
const SfFormat SF_BINARY128 = {15, 112, false, 16};

/* --- 128-bit helpers (no __int128: the repo is strict C11) --------------- */

static void u128_shl(uint64_t *hi, uint64_t *lo, int n)
{
    if (n <= 0)
        return;
    if (n >= 128) {
        *hi = 0;
        *lo = 0;
    } else if (n >= 64) {
        *hi = *lo << (n - 64);
        *lo = 0;
    } else {
        *hi = (*hi << n) | (*lo >> (64 - n));
        *lo <<= n;
    }
}

/* Shifts right, returning true if any 1 bit was shifted out (the STICKY
 * flag — the bit that makes round-to-nearest-even correct rather than
 * approximately correct). */
static bool u128_shr_sticky(uint64_t *hi, uint64_t *lo, int n)
{
    bool sticky = false;
    int i;

    if (n <= 0)
        return false;
    if (n >= 128) {
        sticky = (*hi | *lo) != 0;
        *hi = 0;
        *lo = 0;
        return sticky;
    }
    for (i = 0; i < n; i++) {
        if (*lo & 1)
            sticky = true;
        *lo = (*lo >> 1) | (*hi << 63);
        *hi >>= 1;
    }
    return sticky;
}

static int u128_bitlen(uint64_t hi, uint64_t lo)
{
    int n = 0;

    if (hi) {
        while (hi) {
            n++;
            hi >>= 1;
        }
        return n + 64;
    }
    while (lo) {
        n++;
        lo >>= 1;
    }
    return n;
}

static void u128_add(uint64_t *hi, uint64_t *lo, uint64_t ahi, uint64_t alo)
{
    uint64_t old = *lo;

    *lo += alo;
    *hi += ahi + (*lo < old ? 1 : 0);
}

static int u128_cmp(uint64_t ahi, uint64_t alo, uint64_t bhi, uint64_t blo)
{
    if (ahi != bhi)
        return ahi < bhi ? -1 : 1;
    if (alo != blo)
        return alo < blo ? -1 : 1;
    return 0;
}

static void u128_sub(uint64_t *hi, uint64_t *lo, uint64_t bhi, uint64_t blo)
{
    uint64_t old = *lo;

    *lo -= blo;
    *hi -= bhi + (old < blo ? 1 : 0);
}

/* --- format geometry ----------------------------------------------------- */

/* Total significand bits, INCLUDING the implicit leading one. */
static int prec_of(SfFormat f)
{
    return f.explicit_intbit ? f.frac_bits : f.frac_bits + 1;
}

static int emax_of(SfFormat f)
{
    return (1 << (f.exp_bits - 1)) - 1;
}

static int emin_of(SfFormat f)
{
    return 1 - emax_of(f);
}

/* --- rounding ------------------------------------------------------------ */

/* Rounds the integer significand (hi:lo) * 2^exp into `f`, round-to-
 * nearest-even, honouring subnormals and overflow. This is the ONE place
 * rounding happens: every other operation computes exactly (or with a
 * sticky bit) and finishes here, so no value is ever rounded twice. */
static Sf round_pack(uint8_t sign, uint64_t hi, uint64_t lo, int32_t exp,
                     bool sticky_in, SfFormat f, SfStatus *st)
{
    int prec = prec_of(f);
    int bits;
    Sf r;
    bool sticky = sticky_in;
    bool round_bit = false;
    int emin = emin_of(f);
    int emax = emax_of(f);

    memset(&r, 0, sizeof(r));
    r.sign = sign;

    if (hi == 0 && lo == 0 && !sticky) {
        /* An exact cancellation gives +0 under round-to-nearest, whichever
         * way the operands were signed. sf_add handles -0 + -0 before it
         * ever reaches here. */
        r.cls = SF_ZERO;
        r.sign = 0;
        return r;
    }

    bits = u128_bitlen(hi, lo);

    /* Normalize to exactly `prec` significant bits, collecting the round
     * bit and sticky on the way down. */
    if (bits > prec) {
        int shift = bits - prec;

        /* The bit immediately below the retained field decides the tie. */
        round_bit = ((shift <= 64) ? ((lo >> (shift - 1)) & 1)
                                   : ((hi >> (shift - 65)) & 1)) != 0;
        if (u128_shr_sticky(&hi, &lo, shift - 1))
            sticky = true;
        /* One more shift drops the round bit itself (already captured
         * above), leaving exactly `prec` bits retained. */
        u128_shr_sticky(&hi, &lo, 1);
        exp += shift;
    } else if (bits < prec) {
        u128_shl(&hi, &lo, prec - bits);
        exp -= prec - bits;
    }

    /* Subnormal range: the exponent cannot go below emin, so shift the
     * significand right instead and lose precision — which is exactly
     * what makes 5e-324 the smallest positive double rather than zero. */
    if (exp < emin - (prec - 1)) {
        int shift = (emin - (prec - 1)) - exp;

        if (shift >= 128) {
            st->underflow = true;
            st->inexact = true;
            r.cls = SF_ZERO;
            return r;
        }
        if (round_bit)
            sticky = true;
        round_bit = ((shift <= 64) ? ((lo >> (shift - 1)) & 1)
                                   : ((hi >> (shift - 65)) & 1)) != 0;
        if (u128_shr_sticky(&hi, &lo, shift - 1))
            sticky = true;
        u128_shr_sticky(&hi, &lo, 1);
        exp += shift;
        st->underflow = true;
    }

    /* Round to nearest, ties to EVEN. */
    if (round_bit && (sticky || (lo & 1))) {
        uint64_t ohi = hi;

        u128_add(&hi, &lo, 0, 1);
        /* A carry out of the top widens the significand by one bit. */
        if (u128_bitlen(hi, lo) > prec) {
            u128_shr_sticky(&hi, &lo, 1);
            exp++;
        }
        (void)ohi;
    }
    if (round_bit || sticky)
        st->inexact = true;

    /* An exact cancellation gives +0 under round-to-nearest, whichever
     * way the operands were signed — only -0 + -0 stays negative, and
     * that case never reaches here. */
    if (hi == 0 && lo == 0)
        r.sign = 0;

    /* Overflow to infinity. */
    if (exp + (prec - 1) > emax) {
        st->overflow = true;
        st->inexact = true;
        r.cls = SF_INF;
        return r;
    }

    if (hi == 0 && lo == 0) {
        r.cls = SF_ZERO;
        return r;
    }
    r.cls = SF_NORMAL;
    r.hi = hi;
    r.lo = lo;
    r.exp = exp;
    return r;
}

/* --- decimal to binary --------------------------------------------------- */

Sf sf_from_decimal(const char *digits, size_t n, int32_t dec_exp, SfFormat f,
                   SfStatus *st)
{
    static BigInt num, den; /* ~6 KB each: too large for the stack */
    int prec = prec_of(f);
    int qbits = prec + 2; /* guard + round; sticky comes from the remainder */
    uint64_t qhi, qlo;
    bool sticky;
    size_t i;
    int nbits, dbits, shift;
    Sf r;
    size_t lead = 0;

    memset(&r, 0, sizeof(r));

    /* Skip leading zeros so `0.0000...1` does not inflate the bignum. */
    while (lead < n && digits[lead] == '0')
        lead++;
    if (lead == n) {
        r.cls = SF_ZERO;
        return r;
    }

    bigint_set_u64(&num, 0);
    for (i = lead; i < n; i++) {
        bigint_mul_small(&num, 10);
        if (num.len == 0)
            bigint_set_u64(&num, (uint64_t)(digits[i] - '0'));
        else
            num.w[0] += (uint64_t)(digits[i] - '0'); /* < 10, cannot carry */
    }
    bigint_set_u64(&den, 1);

    /* value = num * 10^dec_exp, expressed as num/den. */
    if (dec_exp > 0)
        bigint_mul_pow10(&num, dec_exp);
    else if (dec_exp < 0)
        bigint_mul_pow10(&den, -dec_exp);

    if (num.overflow || den.overflow) {
        /* Refusing is the honest answer: a truncated bignum is a wrong
         * constant, and the caller reports it as out of range. */
        st->overflow = true;
        st->inexact = true;
        r.cls = SF_INF;
        return r;
    }

    /* Scale so the quotient has exactly `qbits` bits. */
    nbits = bigint_bitlen(&num);
    dbits = bigint_bitlen(&den);
    shift = qbits - (nbits - dbits);
    if (shift > 0)
        bigint_shl(&num, shift);
    else if (shift < 0)
        bigint_shl(&den, -shift);
    if (num.overflow || den.overflow) {
        st->overflow = true;
        r.cls = SF_INF;
        return r;
    }

    bigint_divide_bits(&num, &den, qbits, &qhi, &qlo, &sticky);
    /* The quotient carries `qbits` (or qbits+1) significant bits and the
     * scaling applied above is undone by the exponent; round_pack
     * normalizes either width and rounds ONCE. */
    return round_pack(0, qhi, qlo, -shift, sticky, f, st);
}

Sf sf_from_hex(const char *hexdigits, size_t n, int32_t bin_exp, SfFormat f,
               SfStatus *st)
{
    uint64_t hi = 0, lo = 0;
    bool sticky = false;
    int32_t exp = bin_exp;
    size_t i;
    Sf r;

    memset(&r, 0, sizeof(r));
    for (i = 0; i < n; i++) {
        int d;
        char c = hexdigits[i];

        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            continue;
        /* Once the significand is full, further digits only matter for
         * the sticky bit — they can never change the retained bits except
         * through rounding. */
        if (u128_bitlen(hi, lo) + 4 > 126) {
            if (d)
                sticky = true;
            exp += 4;
            continue;
        }
        u128_shl(&hi, &lo, 4);
        u128_add(&hi, &lo, 0, (uint64_t)d);
    }
    if (hi == 0 && lo == 0 && !sticky) {
        r.cls = SF_ZERO;
        return r;
    }
    return round_pack(0, hi, lo, exp, sticky, f, st);
}

Sf sf_from_int(uint64_t v, bool neg, SfFormat f, SfStatus *st)
{
    Sf r;

    memset(&r, 0, sizeof(r));
    if (v == 0) {
        r.cls = SF_ZERO;
        r.sign = neg ? 1 : 0;
        return r;
    }
    return round_pack(neg ? 1 : 0, 0, v, 0, false, f, st);
}

/* --- arithmetic ---------------------------------------------------------- */

Sf sf_neg(Sf a)
{
    a.sign = !a.sign;
    return a;
}

bool sf_is_zero(Sf a)
{
    return a.cls == SF_ZERO;
}

static Sf make_nan(SfStatus *st)
{
    Sf r;

    memset(&r, 0, sizeof(r));
    r.cls = SF_NAN;
    st->invalid = true;
    return r;
}

Sf sf_add(Sf a, Sf b, SfFormat f, SfStatus *st)
{
    int32_t exp;
    uint64_t hi, lo;
    uint8_t sign;
    bool sticky = false;
    int diff;

    if (a.cls == SF_NAN || b.cls == SF_NAN) {
        Sf r;
        memset(&r, 0, sizeof(r));
        r.cls = SF_NAN;
        return r;
    }
    if (a.cls == SF_INF || b.cls == SF_INF) {
        if (a.cls == SF_INF && b.cls == SF_INF && a.sign != b.sign)
            return make_nan(st); /* inf - inf */
        return a.cls == SF_INF ? a : b;
    }
    if (a.cls == SF_ZERO && b.cls == SF_ZERO) {
        Sf r;
        memset(&r, 0, sizeof(r));
        r.cls = SF_ZERO;
        /* -0 + -0 is -0; every other zero sum is +0 under round-to
         * -nearest. */
        r.sign = (a.sign && b.sign) ? 1 : 0;
        return r;
    }
    if (a.cls == SF_ZERO)
        return round_pack(b.sign, b.hi, b.lo, b.exp, false, f, st);
    if (b.cls == SF_ZERO)
        return round_pack(a.sign, a.hi, a.lo, a.exp, false, f, st);

    /* Align the exponents, keeping every bit that falls off as sticky —
     * the alignment shift is where a naive implementation loses the
     * information that decides the final rounding. */
    if (a.exp < b.exp) {
        Sf t = a;
        a = b;
        b = t;
    }
    diff = a.exp - b.exp;
    hi = a.hi;
    lo = a.lo;
    /* Give the larger operand headroom so the shift cannot lose its own
     * bits, then bring the smaller one down to match. */
    {
        int head = 126 - u128_bitlen(hi, lo);

        if (head > 0) {
            int up = head < diff ? head : diff;

            u128_shl(&hi, &lo, up);
            a.exp -= up;
            diff -= up;
        }
    }
    {
        uint64_t bhi = b.hi, blo = b.lo;

        if (u128_shr_sticky(&bhi, &blo, diff))
            sticky = true;
        exp = a.exp;
        if (a.sign == b.sign) {
            sign = a.sign;
            u128_add(&hi, &lo, bhi, blo);
        } else if (u128_cmp(hi, lo, bhi, blo) >= 0) {
            sign = a.sign;
            u128_sub(&hi, &lo, bhi, blo);
            /* A borrow against the sticky bits leaves the result one ulp
             * high; correcting here keeps the single-rounding property. */
            if (sticky) {
                u128_sub(&hi, &lo, 0, 1);
                u128_add(&hi, &lo, 0, 0);
            }
        } else {
            sign = b.sign;
            {
                uint64_t rhi = bhi, rlo = blo;

                u128_sub(&rhi, &rlo, hi, lo);
                hi = rhi;
                lo = rlo;
            }
        }
    }
    return round_pack(sign, hi, lo, exp, sticky, f, st);
}

Sf sf_sub(Sf a, Sf b, SfFormat f, SfStatus *st)
{
    return sf_add(a, sf_neg(b), f, st);
}

/* An exact 128x128 -> 256 bit product, in 32-bit limbs. Exactness is the
 * requirement: a shift-and-add loop that squeezes the running product
 * back into 128 bits has to guess when to shift, and the first version of
 * this function guessed wrong — every product came out 2^52 too small. */
static void mul_256(uint64_t ahi, uint64_t alo, uint64_t bhi, uint64_t blo,
                    uint64_t out[4])
{
    uint32_t x[4], y[4];
    uint64_t acc[8];
    int i, j;

    x[0] = (uint32_t)alo;
    x[1] = (uint32_t)(alo >> 32);
    x[2] = (uint32_t)ahi;
    x[3] = (uint32_t)(ahi >> 32);
    y[0] = (uint32_t)blo;
    y[1] = (uint32_t)(blo >> 32);
    y[2] = (uint32_t)bhi;
    y[3] = (uint32_t)(bhi >> 32);
    memset(acc, 0, sizeof(acc));

    for (i = 0; i < 4; i++) {
        uint64_t carry = 0;

        for (j = 0; j < 4; j++) {
            uint64_t cur = acc[i + j] + (uint64_t)x[i] * y[j] + carry;

            acc[i + j] = cur & 0xffffffffull;
            carry = cur >> 32;
        }
        acc[i + 4] += carry;
    }
    out[0] = acc[0] | (acc[1] << 32);
    out[1] = acc[2] | (acc[3] << 32);
    out[2] = acc[4] | (acc[5] << 32);
    out[3] = acc[6] | (acc[7] << 32);
}

static int bitlen_256(const uint64_t p[4])
{
    int i;

    for (i = 3; i >= 0; i--)
        if (p[i]) {
            uint64_t v = p[i];
            int n = 0;

            while (v) {
                v >>= 1;
                n++;
            }
            return i * 64 + n;
        }
    return 0;
}

static bool low_bits_set_256(const uint64_t p[4], int d)
{
    int w = d / 64, b = d % 64, i;

    for (i = 0; i < w && i < 4; i++)
        if (p[i])
            return true;
    if (b && w < 4 && (p[w] & ((1ull << b) - 1)))
        return true;
    return false;
}

static void shr_256(uint64_t p[4], int d)
{
    int w = d / 64, b = d % 64, i;

    if (w) {
        for (i = 0; i + w < 4; i++)
            p[i] = p[i + w];
        for (; i < 4; i++)
            p[i] = 0;
    }
    if (b) {
        for (i = 0; i < 3; i++)
            p[i] = (p[i] >> b) | (p[i + 1] << (64 - b));
        p[3] >>= b;
    }
}

Sf sf_mul(Sf a, Sf b, SfFormat f, SfStatus *st)
{
    uint8_t sign = a.sign ^ b.sign;
    uint64_t p[4];
    uint64_t hi, lo;
    bool sticky = false;
    int32_t pexp;
    Sf r;
    int top;

    if (a.cls == SF_NAN || b.cls == SF_NAN) {
        memset(&r, 0, sizeof(r));
        r.cls = SF_NAN;
        return r;
    }
    if (a.cls == SF_INF || b.cls == SF_INF) {
        if (a.cls == SF_ZERO || b.cls == SF_ZERO)
            return make_nan(st); /* inf * 0 */
        memset(&r, 0, sizeof(r));
        r.cls = SF_INF;
        r.sign = sign;
        return r;
    }
    if (a.cls == SF_ZERO || b.cls == SF_ZERO) {
        memset(&r, 0, sizeof(r));
        r.cls = SF_ZERO;
        r.sign = sign;
        return r;
    }

    /* The significands are integers, so the product's exponent is simply
     * the sum — no correction term. */
    mul_256(a.hi, a.lo, b.hi, b.lo, p);
    pexp = a.exp + b.exp;

    /* Fold the 256-bit product down to the 128 bits round_pack wants,
     * keeping everything shifted out as sticky so the value is rounded
     * exactly once.
     *
     * This has to be driven by the product's BIT LENGTH. The first version
     * walked a 128-bit window down one bit at a time and used the step count
     * as the shift, which overshot 63 and then fed an out-of-range count to
     * `>>` — undefined behaviour. It never showed up because no format
     * reached it: a binary64 product is at most 106 bits and an x87-80 one
     * at most 128, so both stay inside p[1]:p[0]. binary128 has a 113-bit
     * significand, so its product always spills into p[3] and always took
     * the broken path. */
    {
        int len = bitlen_256(p);

        if (len > 128) {
            int drop = len - 128;

            sticky = low_bits_set_256(p, drop);
            shr_256(p, drop);
            pexp += drop;
        }
        hi = p[1];
        lo = p[0];
    }
    top = u128_bitlen(hi, lo);
    (void)top;
    return round_pack(sign, hi, lo, pexp, sticky, f, st);
}

Sf sf_div(Sf a, Sf b, SfFormat f, SfStatus *st)
{
    int prec = prec_of(f);
    int qbits = prec + 2;
    uint64_t qhi = 0, qlo = 0;
    uint64_t rhi, rlo;
    uint64_t dhi, dlo;
    uint8_t sign = a.sign ^ b.sign;
    int32_t aexp = 0, bexp = 0;
    int i;
    Sf r;

    if (a.cls == SF_NAN || b.cls == SF_NAN) {
        memset(&r, 0, sizeof(r));
        r.cls = SF_NAN;
        return r;
    }
    if (a.cls == SF_INF && b.cls == SF_INF)
        return make_nan(st);
    if (a.cls == SF_ZERO && b.cls == SF_ZERO)
        return make_nan(st);
    if (b.cls == SF_ZERO) {
        memset(&r, 0, sizeof(r));
        r.cls = SF_INF;
        r.sign = sign;
        st->invalid = true; /* the caller decides whether this is an error */
        return r;
    }
    if (a.cls == SF_ZERO || b.cls == SF_INF) {
        memset(&r, 0, sizeof(r));
        r.cls = SF_ZERO;
        r.sign = sign;
        return r;
    }
    if (a.cls == SF_INF) {
        memset(&r, 0, sizeof(r));
        r.cls = SF_INF;
        r.sign = sign;
        return r;
    }

    /* Both significands are normalized to the SAME width first. Without
     * that the restoring loop below is unbounded: a subnormal operand
     * arrives with a tiny significand — the minimum subnormal's is
     * literally 1 — so the remainder never shrinks and doubles every
     * iteration until it will not fit.
     *
     * The first version coped by BREAKING out of the loop at that point,
     * which silently produced fewer quotient bits than the exponent
     * adjustment below assumes. 1.0 / DBL_MIN_SUBNORMAL came back as a
     * large finite number instead of overflowing to infinity, in every
     * format. Normalizing removes the condition rather than detecting it.
     *
     * 120 leaves headroom: after a subtract the remainder is below the
     * divisor, so the following shift cannot exceed 121 bits. */
    {
        int ka = 120 - u128_bitlen(a.hi, a.lo);
        int kb = 120 - u128_bitlen(b.hi, b.lo);

        rhi = a.hi;
        rlo = a.lo;
        dhi = b.hi;
        dlo = b.lo;
        if (ka > 0)
            u128_shl(&rhi, &rlo, ka);
        else if (ka < 0)
            u128_shr_sticky(&rhi, &rlo, -ka);
        if (kb > 0)
            u128_shl(&dhi, &dlo, kb);
        else if (kb < 0)
            u128_shr_sticky(&dhi, &dlo, -kb);
        aexp = a.exp - ka;
        bexp = b.exp - kb;
    }
    /* Restoring division, one quotient bit at a time to prec+2 bits, with
     * the final remainder as sticky — the same shape as the decimal
     * path, and for the same reason: obviously correct beats clever. */
    for (i = 0; i < qbits; i++) {
        qhi = (qhi << 1) | (qlo >> 63);
        qlo <<= 1;
        if (u128_cmp(rhi, rlo, dhi, dlo) >= 0) {
            u128_sub(&rhi, &rlo, dhi, dlo);
            qlo |= 1;
        }
        if (i + 1 < qbits)
            u128_shl(&rhi, &rlo, 1);
    }
    /* The loop produced qbits bits of (sig_a / sig_b) scaled up by
     * 2^(qbits-1), so that scaling comes back out of the exponent. */
    return round_pack(sign, qhi, qlo, aexp - bexp - (qbits - 1),
                      (rhi | rlo) != 0, f, st);
}

Sf sf_convert(Sf a, SfFormat from, SfFormat to, SfStatus *st)
{
    (void)from;
    if (a.cls != SF_NORMAL)
        return a;
    return round_pack(a.sign, a.hi, a.lo, a.exp, false, to, st);
}

int sf_cmp(Sf a, Sf b, bool *unordered)
{
    *unordered = false;
    if (a.cls == SF_NAN || b.cls == SF_NAN) {
        *unordered = true;
        return 0;
    }
    if (a.cls == SF_ZERO && b.cls == SF_ZERO)
        return 0; /* +0 == -0 */
    if (a.sign != b.sign)
        return a.sign ? -1 : 1;
    if (a.cls == SF_INF && b.cls == SF_INF)
        return 0;
    if (a.cls == SF_INF)
        return a.sign ? -1 : 1;
    if (b.cls == SF_INF)
        return b.sign ? 1 : -1;
    if (a.cls == SF_ZERO)
        return b.sign ? 1 : -1;
    if (b.cls == SF_ZERO)
        return a.sign ? -1 : 1;
    {
        /* Compare magnitudes by aligning to a common exponent. */
        int abits = u128_bitlen(a.hi, a.lo) + a.exp;
        int bbits = u128_bitlen(b.hi, b.lo) + b.exp;
        int c;

        if (abits != bbits)
            c = abits < bbits ? -1 : 1;
        else {
            uint64_t ahi = a.hi, alo = a.lo, bhi = b.hi, blo = b.lo;
            int shift = a.exp - b.exp;

            if (shift > 0)
                u128_shl(&ahi, &alo, shift);
            else if (shift < 0)
                u128_shl(&bhi, &blo, -shift);
            c = u128_cmp(ahi, alo, bhi, blo);
        }
        return a.sign ? -c : c;
    }
}

uint64_t sf_to_int(Sf a, int width, bool is_unsigned, SfStatus *st)
{
    uint64_t hi, lo;
    uint64_t limit;
    int shift;

    if (width < 1 || width > 64) {
        st->invalid = true;
        return 0;
    }
    if (a.cls == SF_ZERO)
        return 0;
    if (a.cls != SF_NORMAL) {
        st->invalid = true;
        return 0;
    }
    hi = a.hi;
    lo = a.lo;
    shift = a.exp;
    if (shift > 0) {
        if (u128_bitlen(hi, lo) + shift > 64) {
            st->invalid = true; /* out of range: 6.3.1.4 makes this UB */
            return 0;
        }
        u128_shl(&hi, &lo, shift);
    } else if (shift < 0) {
        /* C truncates TOWARD ZERO, so the shifted-out bits are simply
         * discarded rather than rounded. */
        if (u128_shr_sticky(&hi, &lo, -shift))
            st->inexact = true;
    }
    if (hi != 0) {
        st->invalid = true;
        return 0;
    }

    /* 6.3.1.4 discards the fractional part first, then requires the
     * remaining integral value to fit the destination type.  Checking
     * before masking is load-bearing: the old code silently turned 128.0
     * into signed i8 -128 and -1.0 into UINT_MAX.  A negative value whose
     * truncated magnitude is zero is representable as either signed or
     * unsigned zero. */
    if (is_unsigned) {
        limit = width == 64 ? UINT64_MAX : (1ull << width) - 1;
        if ((a.sign && lo != 0) || lo > limit) {
            st->invalid = true;
            return 0;
        }
    } else {
        limit = 1ull << (width - 1);
        if ((!a.sign && lo >= limit) || (a.sign && lo > limit)) {
            st->invalid = true;
            return 0;
        }
    }
    if (!a.sign)
        return lo;
    lo = (uint64_t)(0 - lo);
    return width == 64 ? lo : lo & ((1ull << width) - 1);
}

/* --- bit images ---------------------------------------------------------- */

void sf_to_bits(Sf a, SfFormat f, uint8_t out[16])
{
    int prec = prec_of(f);
    int emax = emax_of(f);
    int emin = emin_of(f);
    uint64_t frac_hi = 0, frac_lo = 0;
    int32_t biased = 0;
    int i;
    int total_bits = f.exp_bits + f.frac_bits + 1;

    memset(out, 0, 16);
    switch ((SfClass)a.cls) {
    case SF_ZERO:
        biased = 0;
        break;
    case SF_INF:
        biased = (1 << f.exp_bits) - 1;
        break;
    case SF_NAN:
        biased = (1 << f.exp_bits) - 1;
        /* A quiet NaN sets the top fraction bit. */
        if (f.frac_bits > 64)
            frac_hi = 1ull << (f.frac_bits - 64 - 1);
        else
            frac_lo = 1ull << (f.frac_bits - 1);
        break;
    case SF_NORMAL: {
        uint64_t hi = a.hi, lo = a.lo;
        /* Derive the exponent from the significand's ACTUAL width, not
         * from `prec`: round_pack leaves a subnormal with fewer than
         * `prec` bits, and assuming a normalized width there wrote biased
         * exponent 1 instead of 0 — turning the smallest double into
         * 2^-1022 * (1 + 2^-52). */
        int msb = u128_bitlen(hi, lo);
        int unbiased = a.exp + (msb > 0 ? msb - 1 : 0);

        if (unbiased < emin) {
            /* Subnormal: the exponent field is zero and the significand
             * is positioned against emin rather than normalized. */
            int target = emin - (prec - 1);

            if (a.exp > target)
                u128_shl(&hi, &lo, a.exp - target);
            else if (a.exp < target)
                u128_shr_sticky(&hi, &lo, target - a.exp);
            biased = 0;
        } else {
            biased = unbiased + emax;
            /* Renormalize so the implicit bit sits exactly at prec-1. */
            {
                int want = prec - 1;

                if (msb - 1 < want)
                    u128_shl(&hi, &lo, want - (msb - 1));
                else if (msb - 1 > want)
                    u128_shr_sticky(&hi, &lo, (msb - 1) - want);
            }
            if (!f.explicit_intbit) {
                /* Drop the implicit leading one. */
                if (prec - 1 >= 64)
                    hi &= (1ull << (prec - 1 - 64)) - 1;
                else
                    lo &= (prec - 1 >= 64) ? ~0ull : ((1ull << (prec - 1)) - 1);
            }
        }
        frac_hi = hi;
        frac_lo = lo;
        break;
    }
    }

    /* Assemble little-endian: fraction, then exponent, then sign. */
    for (i = 0; i < 8 && i < f.total_bytes; i++)
        out[i] = (uint8_t)(frac_lo >> (i * 8));
    for (i = 8; i < 16 && i < f.total_bytes; i++)
        out[i] = (uint8_t)(frac_hi >> ((i - 8) * 8));
    for (i = 0; i < f.exp_bits; i++) {
        int bit = f.frac_bits + i;

        if ((biased >> i) & 1)
            out[bit / 8] |= (uint8_t)(1u << (bit % 8));
    }
    if (a.sign) {
        int bit = total_bits - 1;

        out[bit / 8] |= (uint8_t)(1u << (bit % 8));
    }
}

Sf sf_from_bits(const uint8_t in[16], SfFormat f)
{
    int prec = prec_of(f);
    int emax = emax_of(f);
    int total_bits = f.exp_bits + f.frac_bits + 1;
    uint64_t frac_lo = 0, frac_hi = 0;
    int32_t biased = 0;
    Sf r;
    int i;

    memset(&r, 0, sizeof(r));
    for (i = 0; i < 8 && i < f.total_bytes; i++)
        frac_lo |= (uint64_t)in[i] << (i * 8);
    for (i = 8; i < 16 && i < f.total_bytes; i++)
        frac_hi |= (uint64_t)in[i] << ((i - 8) * 8);
    /* Mask the fraction to its own width. */
    if (f.frac_bits < 64) {
        frac_lo &= (1ull << f.frac_bits) - 1;
        frac_hi = 0;
    } else if (f.frac_bits < 128) {
        if (f.frac_bits - 64 < 64)
            frac_hi &=
                (f.frac_bits == 64) ? 0 : ((1ull << (f.frac_bits - 64)) - 1);
    }
    for (i = 0; i < f.exp_bits; i++) {
        int bit = f.frac_bits + i;

        if (in[bit / 8] & (1u << (bit % 8)))
            biased |= 1 << i;
    }
    r.sign = (in[(total_bits - 1) / 8] >> ((total_bits - 1) % 8)) & 1;

    if (biased == (1 << f.exp_bits) - 1) {
        r.cls = (frac_hi | frac_lo) ? SF_NAN : SF_INF;
        return r;
    }
    if (biased == 0 && (frac_hi | frac_lo) == 0) {
        r.cls = SF_ZERO;
        return r;
    }
    r.cls = SF_NORMAL;
    if (biased == 0) {
        r.hi = frac_hi;
        r.lo = frac_lo;
        r.exp = emin_of(f) - (prec - 1);
        return r;
    }
    r.hi = frac_hi;
    r.lo = frac_lo;
    if (!f.explicit_intbit) {
        if (prec - 1 >= 64)
            r.hi |= 1ull << (prec - 1 - 64);
        else
            r.lo |= 1ull << (prec - 1);
    }
    r.exp = biased - emax - (prec - 1);
    return r;
}
