#ifndef CGF_UTIL_BIGINT_H
#define CGF_UTIL_BIGINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A fixed-capacity unsigned bignum, sized for exactly one job: scaling a
 * decimal literal against a power of two so decimal-to-binary conversion
 * can be CORRECTLY ROUNDED.
 *
 * Fixed capacity, no allocator, no arena — softfp must stay library-clean
 * because Sprint 49 links it into libcgf_rt as the arm64 fp128 runtime,
 * where there is no compiler infrastructure to call.
 *
 * The capacity covers the worst case we accept: an fp128 subnormal near
 * 1e-4966 needs 10^4966 (~16.5k bits) and the mantissa may carry a few
 * thousand digits of its own. Anything larger is refused rather than
 * silently truncated — a truncated bignum is a wrong constant. */

#define BIGINT_WORDS 768 /* 49152 bits */

typedef struct {
    uint64_t w[BIGINT_WORDS]; /* little-endian: w[0] is least significant */
    int len;                  /* number of significant words, 0 == zero */
    bool overflow;            /* capacity exceeded: the value is unusable */
} BigInt;

void bigint_set_u64(BigInt *b, uint64_t v);
bool bigint_is_zero(const BigInt *b);
int bigint_bitlen(const BigInt *b);
int bigint_cmp(const BigInt *a, const BigInt *b);
void bigint_shl(BigInt *b, int bits);
void bigint_mul_small(BigInt *b, uint32_t m);
/* b += a << shift_words*64; used by the digit accumulator. */
void bigint_mul_pow10(BigInt *b, int e);
void bigint_sub(BigInt *a, const BigInt *b); /* a -= b, requires a >= b */
bool bigint_test_bit(const BigInt *b, int bit);

/* Long division producing exactly `qbits` quotient bits, most significant
 * first, plus a sticky flag for any nonzero remainder. This is the whole
 * reason the bignum exists: doing it bit-at-a-time is obviously correct
 * and fast enough (a few thousand word operations), where a clever
 * shortcut would need its own proof. */
void bigint_divide_bits(BigInt *num, const BigInt *den, int qbits,
                        uint64_t *q_hi, uint64_t *q_lo, bool *sticky);

#endif
