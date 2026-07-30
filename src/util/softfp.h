#ifndef CGF_UTIL_SOFTFP_H
#define CGF_UTIL_SOFTFP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compile-time floating point with ZERO host FPU involvement.
 *
 * Two reasons this exists rather than calling strtod.
 *
 * DETERMINISM. A host `strtod` puts the host's libc — and on x86 possibly
 * the x87 unit's 80-bit intermediates — into our output. The byte
 * -identical bootstrap in Sprint 58 requires that folding a constant give
 * the same bytes on every machine.
 *
 * CROSS-COMPILATION. Folding an arm64 fp128 constant on an x86 host must
 * not round through the host's 64-bit double. Rounding twice is not the
 * same as rounding once, and the torture table exists because that
 * difference is observable in real literals.
 *
 * LIBRARY-CLEAN. Sprint 49 links this into libcgf_rt as the arm64 fp128
 * runtime, so it includes only <stdint.h>, <stdbool.h>, <stddef.h> and
 * <string.h> — no arena, no diagnostics, no sema. Status comes back to
 * the caller rather than being reported. */

typedef struct {
    int exp_bits;
    int frac_bits;        /* stored fraction bits, excluding any implicit 1 */
    bool explicit_intbit; /* x87-80 stores the leading 1 explicitly */
    int total_bytes;
} SfFormat;

extern const SfFormat SF_BINARY32;
extern const SfFormat SF_BINARY64;
extern const SfFormat SF_X87_80;
extern const SfFormat SF_BINARY128;

typedef enum { SF_ZERO, SF_NORMAL, SF_INF, SF_NAN } SfClass;

/* The significand is a 113-bit-capable integer in (hi, lo), normalized so
 * that the value is  (-1)^sign * (hi:lo) * 2^exp. Keeping an INTEGER
 * significand rather than a fraction avoids an entire family of
 * off-by-one errors around the implicit bit. */
typedef struct {
    uint8_t sign;
    uint8_t cls;
    int32_t exp;
    uint64_t hi, lo;
} Sf;

typedef struct {
    bool inexact;
    bool overflow;
    bool underflow;
    bool invalid;
} SfStatus;

/* `digits` is the mantissa with the decimal point removed; `dec_exp` is
 * the power of ten to apply. Correctly rounded, round-to-nearest-even. */
Sf sf_from_decimal(const char *digits, size_t n, int32_t dec_exp, SfFormat f,
                   SfStatus *st);
/* Hex floats are exact by construction, so this rounds exactly once. */
Sf sf_from_hex(const char *hexdigits, size_t n, int32_t bin_exp, SfFormat f,
               SfStatus *st);
Sf sf_from_int(uint64_t v, bool neg, SfFormat f, SfStatus *st);

Sf sf_add(Sf a, Sf b, SfFormat f, SfStatus *st);
Sf sf_sub(Sf a, Sf b, SfFormat f, SfStatus *st);
Sf sf_mul(Sf a, Sf b, SfFormat f, SfStatus *st);
Sf sf_div(Sf a, Sf b, SfFormat f, SfStatus *st);
Sf sf_neg(Sf a);
Sf sf_convert(Sf a, SfFormat from, SfFormat to, SfStatus *st);
int sf_cmp(Sf a, Sf b, bool *unordered);
bool sf_is_zero(Sf a);
/* C's conversion to integer truncates toward zero (6.3.1.4). */
uint64_t sf_to_int(Sf a, int width, bool is_unsigned, SfStatus *st);

/* The target-endian byte image, for Sprint 19's .data emission. All five
 * targets are little-endian. */
void sf_to_bits(Sf a, SfFormat f, uint8_t out[16]);
/* The inverse, for tests and for reading back an image. */
Sf sf_from_bits(const uint8_t in[16], SfFormat f);

#endif
