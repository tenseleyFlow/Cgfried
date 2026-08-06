/* Both halves ours: caller and callee compiled by Cgfried. */
#include <stdarg.h>
int printf(const char *, ...);

static long tally(int n, ...)
{
    va_list ap;
    long t = 0;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        t += va_arg(ap, int);
    va_end(ap);
    return t;
}

static double dsum(int n, ...)
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

int main(void)
{
    printf("tally=%ld\n", tally(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
    printf("dsum=%.2f\n", dsum(4, 0.25, 0.5, 1.0, 2.25));
    return 0;
}
