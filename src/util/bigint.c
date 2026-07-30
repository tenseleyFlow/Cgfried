#include "util/bigint.h"

#include <string.h>

void bigint_set_u64(BigInt *b, uint64_t v)
{
    memset(b, 0, sizeof(*b));
    if (v) {
        b->w[0] = v;
        b->len = 1;
    }
}

bool bigint_is_zero(const BigInt *b)
{
    return b->len == 0;
}

static void trim(BigInt *b)
{
    while (b->len > 0 && b->w[b->len - 1] == 0)
        b->len--;
}

int bigint_bitlen(const BigInt *b)
{
    uint64_t top;
    int bits = 0;

    if (b->len == 0)
        return 0;
    top = b->w[b->len - 1];
    while (top) {
        bits++;
        top >>= 1;
    }
    return (b->len - 1) * 64 + bits;
}

int bigint_cmp(const BigInt *a, const BigInt *b)
{
    int i;

    if (a->len != b->len)
        return a->len < b->len ? -1 : 1;
    for (i = a->len - 1; i >= 0; i--)
        if (a->w[i] != b->w[i])
            return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

bool bigint_test_bit(const BigInt *b, int bit)
{
    int word = bit / 64;

    if (word >= b->len)
        return false;
    return (b->w[word] >> (bit % 64)) & 1;
}

void bigint_shl(BigInt *b, int bits)
{
    int words = bits / 64;
    int rem = bits % 64;
    int i;

    if (b->len == 0 || bits == 0)
        return;
    if (b->len + words + 1 > BIGINT_WORDS) {
        b->overflow = true;
        return;
    }
    if (rem) {
        uint64_t carry = 0;

        for (i = 0; i < b->len; i++) {
            uint64_t v = b->w[i];

            b->w[i] = (v << rem) | carry;
            carry = v >> (64 - rem);
        }
        if (carry) {
            b->w[b->len] = carry;
            b->len++;
        }
    }
    if (words) {
        for (i = b->len - 1; i >= 0; i--)
            b->w[i + words] = b->w[i];
        for (i = 0; i < words; i++)
            b->w[i] = 0;
        b->len += words;
    }
}

void bigint_mul_small(BigInt *b, uint32_t m)
{
    uint64_t carry = 0;
    int i;

    if (b->len == 0 || m == 1)
        return;
    if (m == 0) {
        b->len = 0;
        return;
    }
    for (i = 0; i < b->len; i++) {
        /* 64x32 -> 96 bits, split so no 128-bit type is needed: the repo
         * is strict C11 and __int128 is a GNU extension. */
        uint64_t lo = (b->w[i] & 0xffffffffull) * m + (carry & 0xffffffffull);
        uint64_t hi = (b->w[i] >> 32) * m + (lo >> 32) + (carry >> 32);

        b->w[i] = (lo & 0xffffffffull) | (hi << 32);
        carry = hi >> 32;
    }
    if (carry) {
        if (b->len >= BIGINT_WORDS) {
            b->overflow = true;
            return;
        }
        b->w[b->len] = carry;
        b->len++;
    }
}

void bigint_mul_pow10(BigInt *b, int e)
{
    /* Multiply in chunks of 10^9, the largest power of ten that fits a
     * u32 multiplier. */
    static const uint32_t pow10[10] = {1,         10,        100,     1000,
                                       10000,     100000,    1000000, 10000000,
                                       100000000, 1000000000};

    while (e >= 9 && !b->overflow) {
        bigint_mul_small(b, 1000000000u);
        e -= 9;
    }
    if (e > 0 && !b->overflow)
        bigint_mul_small(b, pow10[e]);
}

void bigint_sub(BigInt *a, const BigInt *b)
{
    uint64_t borrow = 0;
    int i;

    for (i = 0; i < a->len; i++) {
        uint64_t bv = i < b->len ? b->w[i] : 0;
        uint64_t av = a->w[i];
        uint64_t diff = av - bv - borrow;

        borrow = (av < bv + borrow) || (bv + borrow < bv) ? 1 : 0;
        a->w[i] = diff;
    }
    trim(a);
}

/* Long division, the schoolbook shape: walk the dividend's bits from the
 * top, shifting each into a running REMAINDER and asking once per bit
 * whether the divisor fits.
 *
 * The remainder is what gets shifted — not the dividend. Shifting the
 * dividend and comparing the whole of it produces one subtraction where
 * the quotient digit needed many, which silently yields a quotient with
 * only its leading bit set. That was the first version of this function,
 * and it turned 0.1 into 0.125. */
void bigint_divide_bits(BigInt *num, const BigInt *den, int qbits,
                        uint64_t *q_hi, uint64_t *q_lo, bool *sticky)
{
    static BigInt rem; /* ~6 KB: too large for the stack */
    int nb = bigint_bitlen(num);
    uint64_t hi = 0, lo = 0;
    int produced = 0;
    int i;

    (void)qbits;
    bigint_set_u64(&rem, 0);
    for (i = nb - 1; i >= 0; i--) {
        bigint_shl(&rem, 1);
        if (bigint_test_bit(num, i)) {
            if (rem.len == 0) {
                rem.w[0] = 1;
                rem.len = 1;
            } else {
                rem.w[0] |= 1;
            }
        }
        /* Once the first quotient bit has been produced, every later
         * iteration contributes one — including the zeros. */
        if (bigint_cmp(&rem, den) >= 0) {
            bigint_sub(&rem, den);
            hi = (hi << 1) | (lo >> 63);
            lo = (lo << 1) | 1;
            produced++;
        } else if (produced) {
            hi = (hi << 1) | (lo >> 63);
            lo <<= 1;
            produced++;
        }
    }
    *q_hi = hi;
    *q_lo = lo;
    *sticky = !bigint_is_zero(&rem);
}
