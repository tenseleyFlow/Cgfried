// OPT_EQ: all
// Hand-rolled va_arg sum crossing the gp register limit.
// EXIT_CODE: 45
#include <stdarg.h>
static int sum(int n, ...)
{
    va_list ap;
    int s = 0, i;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}
int main(void)
{
    return sum(9, 1, 2, 3, 4, 5, 6, 7, 8, 9);
}
