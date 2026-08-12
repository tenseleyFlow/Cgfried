#include <stdlib.h>
#include <string.h>

#include "lex/lex.h"

/* Numeric constant analysis. The pp-number grammar (Sprint 3) is greedy
 * and permissive; validity is decided HERE. Widths come from IntWidths
 * (target-parameterized) — never from host sizeof. */

/* A pp-number is a FLOAT constant iff it has a '.', or a decimal exponent
 * (e/E±) outside a hex constant, or is hex with a binary exponent (p/P).
 * `0x1e+2` is one greedy pp-number but an INTEGER-shaped one (the 'e' is a
 * hex digit) — and an invalid one, diagnosed by lex_int_const. */
bool lex_ppnum_is_float(const char *sp, u32 len)
{
    bool hex = len > 2 && sp[0] == '0' && (sp[1] == 'x' || sp[1] == 'X');
    bool binary = len > 2 && sp[0] == '0' && (sp[1] == 'b' || sp[1] == 'B');
    u32 i;

    for (i = 0; i < len; i++) {
        if (sp[i] == '.')
            return true;
        if (!hex && !binary && (sp[i] == 'e' || sp[i] == 'E'))
            return true;
        if (hex && (sp[i] == 'p' || sp[i] == 'P'))
            return true;
    }
    return false;
}

static bool fits(u64 v, u32 bits, bool is_signed)
{
    if (bits >= 64)
        return is_signed ? (v >> 63) == 0 : true;
    return is_signed ? v < (1ull << (bits - 1)) : v < (1ull << bits);
}

typedef struct {
    bool suf_u;
    int suf_l; /* 0 none, 1 long, 2 long long */
    bool bad;
} IntSuffix;

static IntSuffix parse_int_suffix(const char *s, u32 len, u32 *end)
{
    IntSuffix r;
    u32 i = *end;

    memset(&r, 0, sizeof(r));
    while (i < len) {
        char c = s[i];

        if ((c == 'u' || c == 'U') && !r.suf_u) {
            r.suf_u = true;
            i++;
        } else if ((c == 'l' || c == 'L') && r.suf_l == 0) {
            if (i + 1 < len && s[i + 1] == c) { /* SAME case: ll or LL */
                r.suf_l = 2;
                i += 2;
            } else if (i + 1 < len && (s[i + 1] == 'l' || s[i + 1] == 'L')) {
                r.bad = true; /* lL / Ll is not a suffix (6.4.4.1) */
                return r;
            } else {
                r.suf_l = 1;
                i++;
            }
        } else {
            r.bad = true;
            return r;
        }
    }
    *end = i;
    return r;
}

void lex_int_const(Preprocessor *pp, Token *t, const char *sp, u32 len,
                   const LangOpts *lang, IntWidths w, SrcLoc loc)
{
    u32 base = 10, i = 0;
    u64 v = 0;
    bool overflow = false;
    IntSuffix suf;
    bool decimal_ladder;

    if (len > 2 && sp[0] == '0' && (sp[1] == 'x' || sp[1] == 'X')) {
        base = 16;
        i = 2;
        if (i >= len) {
            pp_diag_at(pp, DIAG_ERROR, loc, len,
                       "hexadecimal constant has no digits");
            return;
        }
    } else if (len > 2 && sp[0] == '0' && (sp[1] == 'b' || sp[1] == 'B')) {
        base = 2;
        i = 2;
        /* GNU C accepted this spelling long before C23 standardized it.
         * Like gcc, accept it in every language mode and expose it only as
         * a -Wpedantic diagnostic. */
        pp_pedwarn_at(pp, WARN_PEDANTIC, loc, len,
                      "binary constants are a C23 feature or GNU extension");
        if (i >= len) {
            pp_diag_at(pp, DIAG_ERROR, loc, len,
                       "binary constant has no digits");
            return;
        }
    } else if (len > 1 && sp[0] == '0') {
        base = 8;
        i = 1;
    }

    for (; i < len; i++) {
        char c = sp[i];
        u32 d;

        if (c >= '0' && c <= '9')
            d = (u32)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (u32)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (u32)(c - 'A' + 10);
        else
            break;
        if (d >= base) {
            /* `08` and `0b102` are malformed constants, not multiple
             * tokens: pp-number collection was intentionally greedy. */
            pp_diag_at(pp, DIAG_ERROR, loc, len,
                       "invalid digit '%c' in %s constant", c,
                       base == 2 ? "binary" : "octal");
            return;
        }
        if (v > (~(u64)0 - d) / base)
            overflow = true;
        v = v * base + d;
    }

    suf = parse_int_suffix(sp, len, &i);
    if (suf.bad || i != len) {
        pp_diag_at(pp, DIAG_ERROR, loc, len,
                   "invalid suffix on integer constant '%s'", sp);
        return;
    }
    if (overflow) {
        pp_diag_at(pp, DIAG_ERROR, loc, len,
                   "integer constant is too large for any integer type");
        t->int_val = v;
        t->int_type = ITY_ULLONG;
        return;
    }

    t->int_val = v;

    /* THE LADDER (C11 6.4.4.1). Decimal constants climb SIGNED types only;
     * hex/octal may land on unsigned ones at each rung. That asymmetry is
     * why 2147483648 is `long` while 0x80000000 is `unsigned int` on a
     * 32-bit-int target. A too-big DECIMAL is never silently unsigned. */
    decimal_ladder = (base == 10);

    if (suf.suf_u && suf.suf_l == 2) {
        t->int_type = ITY_ULLONG;
    } else if (suf.suf_u && suf.suf_l == 1) {
        t->int_type = fits(v, w.long_bits, false) ? ITY_ULONG : ITY_ULLONG;
    } else if (suf.suf_u) {
        t->int_type = fits(v, w.int_bits, false)    ? ITY_UINT
                      : fits(v, w.long_bits, false) ? ITY_ULONG
                                                    : ITY_ULLONG;
    } else if (suf.suf_l == 2) {
        if (fits(v, w.llong_bits, true))
            t->int_type = ITY_LLONG;
        else if (decimal_ladder)
            goto too_big_decimal;
        else
            t->int_type = ITY_ULLONG;
    } else if (suf.suf_l == 1) {
        if (fits(v, w.long_bits, true))
            t->int_type = ITY_LONG;
        else if (!decimal_ladder && fits(v, w.long_bits, false))
            t->int_type = ITY_ULONG;
        else if (fits(v, w.llong_bits, true))
            t->int_type = ITY_LLONG;
        else if (decimal_ladder)
            goto too_big_decimal;
        else
            t->int_type = ITY_ULLONG;
    } else {
        if (fits(v, w.int_bits, true))
            t->int_type = ITY_INT;
        else if (!decimal_ladder && fits(v, w.int_bits, false))
            t->int_type = ITY_UINT;
        else if (fits(v, w.long_bits, true))
            t->int_type = ITY_LONG;
        else if (!decimal_ladder && fits(v, w.long_bits, false))
            t->int_type = ITY_ULONG;
        else if (decimal_ladder &&
                 (lang->std == STD_C89 || lang->std == STD_GNU89)) {
            /* c89's unsuffixed decimal ladder ends at unsigned long — the
             * one place the ISO ladder itself differs by std. */
            if (fits(v, w.long_bits, false))
                t->int_type = ITY_ULONG;
            else
                goto too_big_decimal;
        } else if (fits(v, w.llong_bits, true))
            t->int_type = ITY_LLONG;
        else if (!decimal_ladder)
            t->int_type = ITY_ULLONG;
        else
            goto too_big_decimal;
    }
    return;

too_big_decimal:
    /* gcc 8 pedwarns ("integer constant is so large that it is unsigned")
     * and uses ullong; we match, and Sprint 37's -pedantic-errors turns it
     * into an error under strict std modes. Never SILENTLY unsigned. */
    pp_pedwarn_at(pp, WARN_OVERFLOW, loc, len,
                  "integer constant is so large that it is unsigned");
    t->int_type = ITY_ULLONG;
}

/* --- floating constants ------------------------------------------------ */

void lex_float_const(Preprocessor *pp, Token *t, const char *sp, u32 len,
                     const LangOpts *lang, TargetSpec target, SrcLoc loc)
{
    bool hex = len > 2 && sp[0] == '0' && (sp[1] == 'x' || sp[1] == 'X');
    u32 i = hex ? 2 : 0;
    bool saw_digit = false, saw_dot = false, saw_exp = false;

    t->float_type = FTY_DOUBLE;

    for (; i < len; i++) {
        char c = sp[i];

        if (c >= '0' && c <= '9') {
            saw_digit = true;
        } else if (hex && !saw_exp &&
                   ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            saw_digit = true;
        } else if (c == '.') {
            if (saw_dot || saw_exp) {
                pp_diag_at(pp, DIAG_ERROR, loc, len,
                           "invalid floating constant '%s'", sp);
                return;
            }
            saw_dot = true;
        } else if ((!hex && (c == 'e' || c == 'E')) ||
                   (hex && (c == 'p' || c == 'P'))) {
            if (saw_exp || !saw_digit) {
                pp_diag_at(pp, DIAG_ERROR, loc, len,
                           "invalid floating constant '%s'", sp);
                return;
            }
            saw_exp = true;
            if (i + 1 < len && (sp[i + 1] == '+' || sp[i + 1] == '-'))
                i++;
            if (i + 1 >= len || sp[i + 1] < '0' || sp[i + 1] > '9') {
                pp_diag_at(pp, DIAG_ERROR, loc, len, "exponent has no digits");
                return;
            }
        } else {
            break; /* suffix */
        }
    }

    if (!saw_digit) {
        pp_diag_at(pp, DIAG_ERROR, loc, len, "invalid floating constant '%s'",
                   sp);
        return;
    }
    /* The binary exponent is REQUIRED for hex floats (6.4.4.2): `0x1.8`
     * alone is an error, not a value. */
    if (hex && !saw_exp) {
        pp_diag_at(pp, DIAG_ERROR, loc, len,
                   "hexadecimal floating constant requires an exponent");
        return;
    }
    if (hex && !std_is_c99_or_later(lang->std))
        pp_warn_at(pp, WARN_C90_C99_COMPAT, loc, len,
                   "hexadecimal floating constants are a C99 feature");

    if (i < len) {
        char c = sp[i];
        if ((c == 'f' || c == 'F') && i + 1 == len)
            t->float_type = FTY_FLOAT;
        else if ((c == 'l' || c == 'L') && i + 1 == len)
            t->float_type = FTY_LDOUBLE;
        /* _Float128 has TWO suffixes and gcc accepts both: the historical
         * `q` (from __float128) and the ISO TS 18661-3 `f128`. Measured:
         * sizeof(1.0Q) and sizeof(1.0F128) are both 16. The f128 form must
         * be tested before the bare `f`, which it is by length. */
        else if ((c == 'q' || c == 'Q') && i + 1 == len) {
            /* GCC's historical `Q` names its target `__float128` mode: a
             * distinct binary128 type on x86, but long double on AArch64
             * Linux. The TS 18661 `F128` suffix below is `_Float128` on
             * both. Apple Clang retains a distinct __float128 mode. */
            t->float_type = target.kind == CGF_TARGET_ARM64_LINUX
                                ? FTY_LDOUBLE
                                : FTY_FLOAT128;
            t->float_ext_suffix = true;
        } else if ((c == 'f' || c == 'F') && len - i == 3 && sp[i + 1] == '3' &&
                   sp[i + 2] == '2') {
            t->float_type = FTY_FLOAT32;
            t->float_ext_suffix = true;
        } else if ((c == 'f' || c == 'F') && len - i == 3 && sp[i + 1] == '6' &&
                   sp[i + 2] == '4') {
            t->float_type = FTY_FLOAT64;
            t->float_ext_suffix = true;
        } else if ((c == 'f' || c == 'F') && len - i == 4 && sp[i + 1] == '3' &&
                   sp[i + 2] == '2' && sp[i + 3] == 'x') {
            t->float_type = FTY_FLOAT32X;
            t->float_ext_suffix = true;
        } else if ((c == 'f' || c == 'F') && len - i == 4 && sp[i + 1] == '6' &&
                   sp[i + 2] == '4' && sp[i + 3] == 'x') {
            t->float_type = FTY_FLOAT64X;
            t->float_ext_suffix = true;
        } else if ((c == 'f' || c == 'F') && len - i == 4 && sp[i + 1] == '1' &&
                   sp[i + 2] == '2' && sp[i + 3] == '8') {
            t->float_type = FTY_FLOAT128;
            t->float_ext_suffix = true;
        } else {
            pp_diag_at(pp, DIAG_ERROR, loc, len,
                       "invalid suffix on floating constant '%s'", sp);
            return;
        }
    }
    /* The VALUE is deliberately not computed here: the exact spelling is
     * the token's payload (t->spelling), and Sprint 15's correctly-rounded
     * engine converts it. See lex_fp_interim's debt note. */
}

/* XD-S08-FPHOST RETIRED (Sprint 15). The host-strtod seam that used to
 * stand here is gone: src/util/softfp.c now converts float spellings
 * correctly-rounded in the TARGET's format, with no host FPU involved.
 * check_bans.sh enforces that no float conversion returned. */
