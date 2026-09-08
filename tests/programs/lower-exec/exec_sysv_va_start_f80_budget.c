/* SysV long double arguments live in 16-byte-aligned memory slots and do not
 * consume an SSE register.  The first function leaves xmm7 available to its
 * anonymous double; the second reaches the overflow area after several f80
 * holes.  Together they pin both halves of va_start's initial cursor state. */
#include <stdarg.h>

static double from_last_sse(double a, double b, double c, double d, double e,
                            double f, double g, long double h, ...)
{
    va_list ap;
    double value;

    va_start(ap, h);
    value = va_arg(ap, double);
    va_end(ap);
    return value;
}

static double from_overflow(double a, double b, double c, double d, double e,
                            double f, double g, long double h, double i,
                            long double j, double k, long double l, double m,
                            long double n, ...)
{
    va_list ap;
    double value;

    va_start(ap, n);
    value = va_arg(ap, double);
    va_end(ap);
    return value;
}

int main(void)
{
    if (from_last_sse(0., 0., 0., 0., 0., 0., 0., 0.L, 1234.) != 1234.)
        return 1;
    if (from_overflow(0., 0., 0., 0., 0., 0., 0., 0.L, 0., 0.L, 0., 0.L, 0.,
                      0.L, 5678.) != 5678.)
        return 2;
    return 0;
}
