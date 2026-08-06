/* Rows 4-7 of the divergence table. Every assertion is a CONSTANT-EXPRESSION
 * check, so clang accepting this same file is the cross-check: no dump
 * format to parse and nothing version-specific. */
#include <float.h>
#include <limits.h>
#include <stddef.h>

/* Row 4: char is SIGNED on arm64-macos, unsigned on arm64-linux. */
_Static_assert((char)-1 < 0, "row 4: char must be signed here");
_Static_assert(CHAR_MIN == -128, "row 4: CHAR_MIN");
_Static_assert(CHAR_MAX == 127, "row 4: CHAR_MAX");

/* Row 5: long double IS double -- the third long-double ABI in the set. */
_Static_assert(sizeof(long double) == 8, "row 5: sizeof");
_Static_assert(_Alignof(long double) == 8, "row 5: alignof");
_Static_assert(LDBL_MANT_DIG == 53, "row 5: mantissa");
_Static_assert(LDBL_MAX_EXP == 1024, "row 5: max exponent");
_Static_assert(LDBL_DIG == DBL_DIG, "row 5: decimal digits");

/* Row 7: wchar_t is int, the same as arm64-linux. */
_Static_assert(sizeof(wchar_t) == 4, "row 7: sizeof");
_Static_assert((wchar_t)-1 < 0, "row 7: signed");

int printf(const char *, ...);

int main(void)
{
    long double ld = 1.0L;
    char c = (char)-1;

    /* Row 5 again, dynamically: if long double were binary128 the sum would
     * differ, and no f128 libcall may appear in the object. */
    ld = ld / 3.0L;
    printf("row4 char=%d\n", (int)c);
    printf("row5 sizeof=%d third=%.17g\n", (int)sizeof(long double),
           (double)ld);
    printf("row7 wchar=%d\n", (int)sizeof(wchar_t));
    /* Row 5, the ABI half: a long double varargs argument takes an ordinary
     * 8-byte slot here, so libc's %Lf reads a double. Getting it wrong is
     * silent -- it prints a plausible number. */
    printf("row5 pct_Lf=%.10Lf %.10Lf\n", 3.14159265358979L, -0.5L);
    return 0;
}

/* Row 5's printf half, kept separate because it is an ABI question rather
 * than a type question: on Apple a `long double` varargs argument occupies
 * an ordinary 8-byte slot, so libc's %Lf reads a double. Getting this wrong
 * is silent -- it prints a plausible number.
 *
 * Called from main above via this forward declaration to keep the file one
 * translation unit with no headers beyond our freestanding three. */
