#include <string.h>

#include "sema/sema.h"

/* Constant expressions (C11 6.6), and the bridge from a float literal's
 * SPELLING to a correctly-rounded value in the target's format.
 *
 * The literal grammar lives here rather than in softfp because softfp
 * must stay library-clean — Sprint 49 links it into libcgf_rt, where
 * there is no C parser. softfp takes digits and an exponent; deciding
 * which characters those are is the compiler's job. */

/* Which format a target uses for each floating type. `long double` is the
 * cross-target trap: x87 80-bit on x86-64, IEEE binary128 on arm64-linux,
 * and plain double on arm64-macos. */
SfFormat constexpr_format_of(Sema *s, const Type *t)
{
    TargetLayout tl = cgf_target_layout(s->target);

    if (!t)
        return SF_BINARY64;
    switch (t->kind) {
    case TY_FLOAT:
        return SF_BINARY32;
    case TY_DOUBLE:
        return SF_BINARY64;
    case TY_LDOUBLE:
        switch (tl.ldbl_kind) {
        case CGF_LDBL_X87_80:
            return SF_X87_80;
        case CGF_LDBL_IEEE128:
            return SF_BINARY128;
        case CGF_LDBL_IS_DOUBLE:
            return SF_BINARY64;
        }
        return SF_BINARY64;
    default:
        return SF_BINARY64;
    }
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Parses a C floating constant's spelling into `f`. The spelling is
 * exactly what the lexer saw, suffix included; the classification (float
 * / double / long double) already happened in Sprint 8, so the FORMAT
 * comes from the caller and the suffix is only skipped here. */
Sf constexpr_parse_float(const char *sp, SfFormat f, SfStatus *st)
{
    char digits[4096];
    size_t ndigits = 0;
    int32_t dec_exp = 0;
    size_t i = 0;
    Sf zero;

    memset(&zero, 0, sizeof(zero));
    memset(st, 0, sizeof(*st));
    if (!sp)
        return zero;

    if (sp[0] == '0' && (sp[1] == 'x' || sp[1] == 'X')) {
        /* Hex float: exact by construction, so it rounds exactly once. */
        int32_t bin_exp = 0;
        bool seen_dot = false;
        size_t frac_digits = 0;

        i = 2;
        while (sp[i] && (is_hex(sp[i]) || sp[i] == '.')) {
            if (sp[i] == '.') {
                seen_dot = true;
            } else {
                if (ndigits < sizeof(digits))
                    digits[ndigits++] = sp[i];
                if (seen_dot)
                    frac_digits++;
            }
            i++;
        }
        if (sp[i] == 'p' || sp[i] == 'P') {
            int sign = 1;
            int32_t v = 0;

            i++;
            if (sp[i] == '+' || sp[i] == '-') {
                if (sp[i] == '-')
                    sign = -1;
                i++;
            }
            while (is_digit(sp[i])) {
                v = v * 10 + (sp[i] - '0');
                i++;
            }
            bin_exp = sign * v;
        }
        /* Each fractional hex digit is four binary places. */
        bin_exp -= (int32_t)(frac_digits * 4);
        return sf_from_hex(digits, ndigits, bin_exp, f, st);
    }

    {
        bool seen_dot = false;
        size_t frac_digits = 0;

        while (sp[i] && (is_digit(sp[i]) || sp[i] == '.')) {
            if (sp[i] == '.') {
                seen_dot = true;
            } else {
                if (ndigits < sizeof(digits))
                    digits[ndigits++] = sp[i];
                if (seen_dot)
                    frac_digits++;
            }
            i++;
        }
        if (sp[i] == 'e' || sp[i] == 'E') {
            int sign = 1;
            int32_t v = 0;

            i++;
            if (sp[i] == '+' || sp[i] == '-') {
                if (sp[i] == '-')
                    sign = -1;
                i++;
            }
            while (is_digit(sp[i])) {
                /* Clamp rather than overflow: an exponent past this is
                 * already infinity or zero in every format we have. */
                if (v < 1000000)
                    v = v * 10 + (sp[i] - '0');
                i++;
            }
            dec_exp = sign * v;
        }
        dec_exp -= (int32_t)frac_digits;
    }
    return sf_from_decimal(digits, ndigits, dec_exp, f, st);
}

/* The value of a float literal token, in the format its type calls for.
 * Diagnoses the range cases gcc diagnoses. */
Sf constexpr_float_literal(Sema *s, AstNode *e)
{
    SfStatus st;
    SfFormat f = constexpr_format_of(s, e->sem_type);
    Sf v = constexpr_parse_float(e->tok ? e->tok->spelling : NULL, f, &st);

    if (st.overflow)
        diag_emit(s->dc, DIAG_WARNING, e->span,
                  "floating constant exceeds range of '%s'",
                  type_to_str(s->arena, e->sem_type));
    else if (st.underflow && st.inexact)
        diag_emit(s->dc, DIAG_WARNING, e->span,
                  "floating constant truncated to zero");
    return v;
}
