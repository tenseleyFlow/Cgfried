/* fpdiff — the float engine's differential.
 *
 * For each literal it prints our correctly-rounded bit pattern in
 * binary32/64. scripts/fp_diff.sh compares that against the SAME literal
 * compiled by gcc into a static initializer and read back out of the
 * object file, so neither side is trusting the other's parser.
 *
 * This tool touches no host FPU: it prints hex patterns produced by
 * softfp. The oracle side does the host conversion, which is the point —
 * they must agree without our ever calling strtod.
 */
#include <stdio.h>
#include <string.h>

#include "util/softfp.h"

/* Duplicated from src/sema/constexpr.c on purpose: this tool must not
 * pull in sema (and therefore the whole compiler) just to parse a
 * literal. Kept deliberately tiny — decimal and hex forms only. */
static Sf parse(const char *sp, SfFormat f, SfStatus *st)
{
    char digits[4096];
    size_t nd = 0;
    int32_t dexp = 0;
    size_t i = 0;
    size_t frac = 0;
    int seen_dot = 0;

    memset(st, 0, sizeof(*st));
    if (sp[0] == '0' && (sp[1] == 'x' || sp[1] == 'X')) {
        int32_t bexp = 0;

        i = 2;
        for (; sp[i] && (sp[i] == '.' || (sp[i] >= '0' && sp[i] <= '9') ||
                         (sp[i] >= 'a' && sp[i] <= 'f') ||
                         (sp[i] >= 'A' && sp[i] <= 'F'));
             i++) {
            if (sp[i] == '.') {
                seen_dot = 1;
            } else {
                if (nd < sizeof(digits))
                    digits[nd++] = sp[i];
                if (seen_dot)
                    frac++;
            }
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
            for (; sp[i] >= '0' && sp[i] <= '9'; i++)
                v = v * 10 + (sp[i] - '0');
            bexp = sign * v;
        }
        return sf_from_hex(digits, nd, bexp - (int32_t)(frac * 4), f, st);
    }
    for (; sp[i] && (sp[i] == '.' || (sp[i] >= '0' && sp[i] <= '9')); i++) {
        if (sp[i] == '.') {
            seen_dot = 1;
        } else {
            if (nd < sizeof(digits))
                digits[nd++] = sp[i];
            if (seen_dot)
                frac++;
        }
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
        for (; sp[i] >= '0' && sp[i] <= '9'; i++)
            if (v < 1000000)
                v = v * 10 + (sp[i] - '0');
        dexp = sign * v;
    }
    return sf_from_decimal(digits, nd, dexp - (int32_t)frac, f, st);
}

static void emit(const char *lit, SfFormat f, int bytes)
{
    SfStatus st;
    Sf v = parse(lit, f, &st);
    uint8_t b[16];
    int i;

    sf_to_bits(v, f, b);
    for (i = bytes - 1; i >= 0; i--)
        printf("%02X", b[i]);
}

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
        emit(argv[i], SF_BINARY32, 4);
        printf(" ");
        emit(argv[i], SF_BINARY64, 8);
        printf("\n");
    }
    return 0;
}
