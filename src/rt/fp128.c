/* libcgf_rt: the arm64 IEEE binary128 soft-float entry points.
 *
 * AAPCS64 Linux makes `long double` IEEE binary128, and no v0.1.0-relevant
 * core implements it in hardware, so every operation is a libcall. Names and
 * signatures are libgcc's, so an object gcc compiled for arm64 links against
 * this archive unchanged — and so that libgcc can serve as the differential
 * oracle (scripts/fp128_diff.sh links the same probe both ways).
 *
 * The arithmetic is src/util/softfp.c, unchanged. Sprint 15 wrote that core
 * "library-clean" — only <stdint.h>, <stdbool.h>, <stddef.h>, <string.h> —
 * specifically so it could be linked in here without dragging the compiler
 * with it. This file is the payoff for that constraint, and it adds no
 * arithmetic of its own: a second implementation would be a second thing to
 * keep correct, and the two would drift.
 *
 * No <stdio.h>/<stdlib.h>: the runtime links into freestanding programs. */

#include "util/softfp.h"

#include <string.h>

/* THE ABI POINT, and the one that a struct-of-two-u64 gets wrong: on AAPCS64
 * a binary128 is an ORDINARY FLOATING-POINT TYPE and travels in a q register
 * (v0-v7), exactly like a float or a double. A 16-byte struct would instead
 * be passed in the x0/x1 pair, so a runtime declaring these arguments as a
 * struct links successfully, runs, and reads its operands out of the wrong
 * registers — arithmetic on garbage, with no crash and no diagnostic.
 *
 * Cgfried spells this ABI type `_Float128`; the host-toolchain build uses
 * libgcc's `mode(TF)` spelling because that is accepted by both gcc and clang
 * across the closed target set. No arithmetic is ever performed on cgf_tf
 * here; it is a carrier, and every value goes through memcpy to reach the
 * softfloat core. */
/* check_bans allow: there is no ISO spelling for binary128, and the ban
 * exists to keep the COMPILER strict C11. The runtime is a separate artifact
 * with its own contract (RT_CFLAGS, plain host toolchain), and getting this
 * type wrong is an ABI break rather than a portability wart. */
#if defined(__CGFRIED__)
typedef _Float128 cgf_tf;
#else
typedef float cgf_tf __attribute__((mode(TF))); /* check_bans allow */
#endif

_Static_assert(sizeof(cgf_tf) == 16, "TF carrier must be IEEE binary128");
_Static_assert(_Alignof(cgf_tf) == 16, "TF carrier must retain ABI alignment");

static Sf tf_in(cgf_tf a)
{
    unsigned char bits[16];

    memcpy(bits, &a, sizeof(bits));
    return sf_from_bits(bits, SF_BINARY128);
}

static cgf_tf tf_out(Sf v)
{
    unsigned char bits[16];
    cgf_tf z;

    sf_to_bits(v, SF_BINARY128, bits);
    memcpy(&z, bits, sizeof(bits));
    return z;
}

static Sf df_in(double a)
{
    unsigned char bits[16];

    memset(bits, 0, sizeof(bits));
    memcpy(bits, &a, sizeof(a));
    return sf_from_bits(bits, SF_BINARY64);
}

static Sf sf_in(float a)
{
    unsigned char bits[16];

    memset(bits, 0, sizeof(bits));
    memcpy(bits, &a, sizeof(a));
    return sf_from_bits(bits, SF_BINARY32);
}

/* --- NaN discipline --------------------------------------------------------
 *
 * Two different behaviours, and conflating them is the trap. When an operand
 * is already NaN, IEEE 754 says PROPAGATE it (quieted); when an operation is
 * merely invalid — 0/0, inf-inf, inf*0 — a fresh NaN is generated, and
 * libgcc's soft-fp generates the all-ones pattern rather than the bare quiet
 * bit. Emitting the bare quiet bit in both cases makes propagation lossy in a
 * way nothing traps on, so the two paths are kept separate here.
 *
 * The softfloat core carries no payload, deliberately: Sprint 31 refused
 * NaN-producing folds for exactly that reason. So payloads are handled at
 * this boundary, on the raw bits, and never enter Sf. */

#define TF_QUIET_BYTE 13 /* bit 111, the mantissa's top bit */
#define TF_QUIET_MASK 0x80u

static bool bits_are_nan(const unsigned char b[16])
{
    unsigned exp = ((unsigned)b[15] & 0x7fu) << 8 | b[14];
    int i;

    if (exp != 0x7fffu)
        return false;
    for (i = 0; i < 14; i++)
        if (b[i])
            return true;
    return false;
}

/* libgcc's generated NaN: every bit below the sign set. */
static cgf_tf tf_default_nan(void)
{
    unsigned char b[16];
    cgf_tf z;
    int i;

    for (i = 0; i < 16; i++)
        b[i] = 0xff;
    b[15] = 0x7f;
    memcpy(&z, b, sizeof(b));
    return z;
}

/* Returns true when the result is decided by a NaN operand. */
static bool nan_passthrough(cgf_tf a, cgf_tf b, cgf_tf *out)
{
    unsigned char ba[16], bb[16];

    memcpy(ba, &a, sizeof(ba));
    memcpy(bb, &b, sizeof(bb));
    if (bits_are_nan(ba)) {
        ba[TF_QUIET_BYTE] |= TF_QUIET_MASK; /* quiet a signaling operand */
        memcpy(out, ba, sizeof(ba));
        return true;
    }
    if (bits_are_nan(bb)) {
        bb[TF_QUIET_BYTE] |= TF_QUIET_MASK;
        memcpy(out, bb, sizeof(bb));
        return true;
    }
    return false;
}

static cgf_tf finish(Sf v, cgf_tf a, cgf_tf b)
{
    cgf_tf passthrough;

    if (nan_passthrough(a, b, &passthrough))
        return passthrough;
    if (v.cls == SF_NAN)
        return tf_default_nan();
    return tf_out(v);
}

/* --- arithmetic ------------------------------------------------------------
 *
 * __negtf2 flips the sign bit and touches nothing else: it is not `0 - x`,
 * which turns -0.0 into +0.0, and it must not go through the softfloat core,
 * which would canonicalize a NaN payload that negation is required to
 * preserve. It is a documented libgcc entry point, so fp128_diff compares it
 * like the rest.
 *
 * It exists as a CALL rather than an inline sign-bit eor because the inline
 * form is a NEON `eor vD.16b`, and the bundled assembler cannot yet encode
 * NEON register operands (the same gap that pins four fixtures in
 * scripts/a64_objdiff_lane.sh). When afs-as gains them, isel can inline this
 * and the symbol stays for ABI compatibility. */
cgf_tf __negtf2(cgf_tf a)
{
    unsigned char bits[16];

    memcpy(bits, &a, sizeof(bits));
    /* binary128 is little-endian on every target in the closed set, so the
     * sign is the top bit of the LAST byte. */
    bits[15] ^= 0x80u;
    memcpy(&a, bits, sizeof(bits));
    return a;
}

cgf_tf __addtf3(cgf_tf a, cgf_tf b)
{
    SfStatus st;

    return finish(sf_add(tf_in(a), tf_in(b), SF_BINARY128, &st), a, b);
}

cgf_tf __subtf3(cgf_tf a, cgf_tf b)
{
    SfStatus st;

    return finish(sf_sub(tf_in(a), tf_in(b), SF_BINARY128, &st), a, b);
}

cgf_tf __multf3(cgf_tf a, cgf_tf b)
{
    SfStatus st;

    return finish(sf_mul(tf_in(a), tf_in(b), SF_BINARY128, &st), a, b);
}

cgf_tf __divtf3(cgf_tf a, cgf_tf b)
{
    SfStatus st;

    return finish(sf_div(tf_in(a), tf_in(b), SF_BINARY128, &st), a, b);
}

/* --- comparisons -----------------------------------------------------------
 *
 * The libgcc contract is a THREE-WAY int, not a boolean, and the unordered
 * answer differs per entry point. Each function must return a value that
 * makes ITS comparison against zero false when either operand is NaN:
 *
 *   a <  b  is  __lttf2(a,b) <  0   so unordered must be positive
 *   a <= b  is  __letf2(a,b) <= 0   so unordered must be positive
 *   a >  b  is  __gttf2(a,b) >  0   so unordered must be negative
 *   a >= b  is  __getf2(a,b) >= 0   so unordered must be negative
 *
 * Returning a single "unordered" sentinel from all four would make exactly
 * two of the four comparisons answer TRUE on NaN, which is the opposite of
 * IEEE 754 and the reason this is spelled out rather than shared. */

static int cmp3(cgf_tf a, cgf_tf b, int unordered_result, bool *was_unordered)
{
    bool unordered = false;
    int r = sf_cmp(tf_in(a), tf_in(b), &unordered);

    if (was_unordered)
        *was_unordered = unordered;
    if (unordered)
        return unordered_result;
    return r < 0 ? -1 : r > 0 ? 1 : 0;
}

/* Zero if and only if the operands are ordered and equal. */
int __eqtf2(cgf_tf a, cgf_tf b)
{
    return cmp3(a, b, 1, 0);
}

int __netf2(cgf_tf a, cgf_tf b)
{
    return cmp3(a, b, 1, 0);
}

int __lttf2(cgf_tf a, cgf_tf b)
{
    return cmp3(a, b, 1, 0);
}

int __letf2(cgf_tf a, cgf_tf b)
{
    return cmp3(a, b, 1, 0);
}

int __gttf2(cgf_tf a, cgf_tf b)
{
    return cmp3(a, b, -1, 0);
}

int __getf2(cgf_tf a, cgf_tf b)
{
    return cmp3(a, b, -1, 0);
}

int __unordtf2(cgf_tf a, cgf_tf b)
{
    bool unordered = false;

    (void)sf_cmp(tf_in(a), tf_in(b), &unordered);
    return unordered ? 1 : 0;
}

/* --- conversions -----------------------------------------------------------
 */

cgf_tf __extenddftf2(double a)
{
    SfStatus st;

    return tf_out(sf_convert(df_in(a), SF_BINARY64, SF_BINARY128, &st));
}

cgf_tf __extendsftf2(float a)
{
    SfStatus st;

    return tf_out(sf_convert(sf_in(a), SF_BINARY32, SF_BINARY128, &st));
}

double __trunctfdf2(cgf_tf a)
{
    SfStatus st;
    unsigned char bits[16];
    double z;

    sf_to_bits(sf_convert(tf_in(a), SF_BINARY128, SF_BINARY64, &st),
               SF_BINARY64, bits);
    memcpy(&z, bits, sizeof(z));
    return z;
}

float __trunctfsf2(cgf_tf a)
{
    SfStatus st;
    unsigned char bits[16];
    float z;

    sf_to_bits(sf_convert(tf_in(a), SF_BINARY128, SF_BINARY32, &st),
               SF_BINARY32, bits);
    memcpy(&z, bits, sizeof(z));
    return z;
}

/* Out-of-range float-to-integer conversion is undefined in C, so the
 * COMPILER diagnoses it when folding. At runtime there is nobody to
 * diagnose to, and libgcc saturates: toward the maximum for a positive or
 * NaN operand and the minimum for a negative one. A drop-in runtime has to
 * agree, because a program built half by gcc and half by us must not see two
 * different answers for the same expression. */
static unsigned long long fix_saturating(cgf_tf a, int width, bool is_unsigned)
{
    SfStatus st;
    Sf v = tf_in(a);
    unsigned long long r;

    memset(&st, 0, sizeof(st));
    r = sf_to_int(v, width, is_unsigned, &st);
    if (!st.invalid)
        return r;
    if (v.sign && v.cls != SF_NAN)
        return is_unsigned
                   ? 0ull
                   : (width == 32
                          ? (unsigned long long)(unsigned)(-2147483647 - 1)
                          : 0x8000000000000000ull);
    if (is_unsigned)
        return width == 32 ? 0xffffffffull : 0xffffffffffffffffull;
    return width == 32 ? 0x7fffffffull : 0x7fffffffffffffffull;
}

int __fixtfsi(cgf_tf a)
{
    return (int)(unsigned int)fix_saturating(a, 32, false);
}

long long __fixtfdi(cgf_tf a)
{
    return (long long)fix_saturating(a, 64, false);
}

unsigned int __fixunstfsi(cgf_tf a)
{
    return (unsigned int)fix_saturating(a, 32, true);
}

unsigned long long __fixunstfdi(cgf_tf a)
{
    return (unsigned long long)fix_saturating(a, 64, true);
}

/* sf_from_int takes a magnitude plus a sign, so a negative input is negated
 * in UNSIGNED arithmetic: -(long long)LLONG_MIN overflows in signed. */
static cgf_tf from_signed(long long a)
{
    SfStatus st;
    unsigned long long mag =
        a < 0 ? 0ull - (unsigned long long)a : (unsigned long long)a;

    return tf_out(sf_from_int(mag, a < 0, SF_BINARY128, &st));
}

cgf_tf __floatsitf(int a)
{
    return from_signed(a);
}

cgf_tf __floatditf(long long a)
{
    return from_signed(a);
}

cgf_tf __floatunsitf(unsigned int a)
{
    SfStatus st;

    return tf_out(sf_from_int(a, false, SF_BINARY128, &st));
}

cgf_tf __floatunditf(unsigned long long a)
{
    SfStatus st;

    return tf_out(sf_from_int(a, false, SF_BINARY128, &st));
}
