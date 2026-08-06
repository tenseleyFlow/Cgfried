/* Compiled by clang: the reference implementation of the callee side. */
#include <stdarg.h>

int sum9(int n, ...)
{
    va_list ap;
    int i, t = 0;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        t += va_arg(ap, int);
    va_end(ap);
    return t;
}

double avg3(int n, ...)
{
    va_list ap;
    int i;
    double t = 0;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        t += va_arg(ap, double);
    va_end(ap);
    return t / n;
}

long mix(int a, int b, ...)
{
    va_list ap;
    long t = a + b;
    int i;

    va_start(ap, b);
    for (i = 0; i < 3; i++)
        t += va_arg(ap, long);
    va_end(ap);
    return t;
}
