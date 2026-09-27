/* clang compiles this CALLEE, so a shared Cgfried caller/callee mistake
 * cannot make the test agree with itself. */
#include <stdarg.h>

struct __attribute__((aligned(32))) V32 { /* check_bans allow: ABI fixture */
    double a, b, c, d;
};

int read_v32(int skip, ...)
{
    struct V32 value;
    va_list ap;

    va_start(ap, skip);
    while (skip--)
        (void)va_arg(ap, int);
    value = va_arg(ap, struct V32);
    va_end(ap);
    if (value.a != 1.25)
        return 1;
    if (value.b != 2.5)
        return 2;
    if (value.c != 4.75)
        return 3;
    if (value.d != 8.0)
        return 4;
    return 0;
}
