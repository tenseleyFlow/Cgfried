// The va_* corner cases the sprint file calls out: va_copy makes an
// INDEPENDENT cursor (both copies must be walkable and both va_end'd),
// and alloca inside a loop must not grow the frame per iteration.
// EXIT_CODE: 0
#include <stdarg.h>
static int sum_twice(int n, ...)
{
    va_list a, b;
    int i, s = 0;

    va_start(a, n);
    va_copy(b, a);
    for (i = 0; i < n; i++)
        s += va_arg(a, int);
    /* b still points at the FIRST variadic argument. */
    for (i = 0; i < n; i++)
        s += va_arg(b, int);
    va_end(b);
    va_end(a);
    return s;
}
int main(void)
{
    int i;
    long total = 0;

    if (sum_twice(4, 1, 2, 3, 4) != 20)
        return 1;
    /* alloca in a loop: each iteration's block is live until the
     * function returns, so the addresses must differ and the writes
     * must not clobber each other. */
    for (i = 0; i < 8; i++) {
        int *p = (int *)__builtin_alloca(sizeof(int) * 4);
        int j;

        for (j = 0; j < 4; j++)
            p[j] = i * 10 + j;
        total += p[0] + p[3];
    }
    if (total != 8 * 3 + 10 * (0 + 1 + 2 + 3 + 4 + 5 + 6 + 7) * 2)
        return 2;
    return 0;
}
