// FLAGS: -std=gnu17
// EXIT_CODE: 0

#include <stdarg.h>

static int first_int(...)
{
    va_list ap;
    int value;

    va_start(ap);
    value = va_arg(ap, int);
    va_end(ap);
    return value;
}

static long long first_long_long(...)
{
    va_list ap;
    long long value;

    va_start(ap);
    value = va_arg(ap, long long);
    va_end(ap);
    return value;
}

static double first_double(...)
{
    va_list ap;
    double value;

    va_start(ap);
    value = va_arg(ap, double);
    va_end(ap);
    return value;
}

int main(void)
{
    if (first_int(-2, 9) != -2)
        return 1;
    if (first_long_long(-3LL, 9) != -3LL)
        return 2;
    if (first_double(4.25, 9) != 4.25)
        return 3;
    return 0;
}
