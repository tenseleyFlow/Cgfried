/* Cgfried compiles this CALLEE; clang compiles the caller. */
#include <stdarg.h>
long rsum(int n, ...)
{
    va_list ap;
    long t = 0;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        t += va_arg(ap, long);
    va_end(ap);
    return t;
}
double rmix(int n, ...)
{
    va_list ap;
    double t = 0;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        t += va_arg(ap, double);
    va_end(ap);
    return t;
}
