// Every <limits.h> and <float.h> macro must be usable in a constant
// expression (they are #if-able and array-bound-able by contract), and
// <stdint.h>'s _C macros must produce the right TYPE, not just the
// right value.
// EXIT_CODE: 0
#include <float.h>
#include <limits.h>
#include <stdint.h>
#if CHAR_BIT != 8
#error "CHAR_BIT must be 8 on every v0.1.0 target"
#endif
#if INT_MAX != 2147483647
#error "INT_MAX"
#endif
#if FLT_MANT_DIG != 24 || DBL_MANT_DIG != 53
#error "IEEE binary32/64 are not optional"
#endif
static char probe1[LLONG_MAX > INT_MAX ? 1 : -1];
static char probe2[UINT64_C(18446744073709551615) > 0 ? 1 : -1];
static char probe3[INT64_C(-9223372036854775807) < 0 ? 1 : -1];
int main(void)
{
    /* The suffix must make the constant WIDE, not merely large. */
    if (sizeof(INT64_C(1)) != 8)
        return 1;
    if (sizeof(UINT64_C(1)) != 8)
        return 2;
    if (sizeof(INT32_C(1)) != 4)
        return 3;
    if (sizeof probe1 + sizeof probe2 + sizeof probe3 != 3)
        return 4;
    /* Signedness of the fast/least types follows the target table. */
    if ((int_fast16_t)-1 >= 0 || (uint_fast16_t)-1 <= 0)
        return 5;
    return 0;
}
