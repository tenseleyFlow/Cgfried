// FLAGS: -O2
// OPT_EQ: all
// CHECK: 42
#include <stdarg.h>

int printf(const char *, ...);

static int sum_args(int count, ...)
{
    va_list ap;
    int sum = 0;
    int i;

    va_start(ap, count);
    for (i = 0; i < count; i++)
        sum += va_arg(ap, int);
    va_end(ap);
    return sum;
}

int main(void)
{
    int result = sum_args(3, 10, 12, 20);

    printf("%d\n", result);
    return result != 42;
}
