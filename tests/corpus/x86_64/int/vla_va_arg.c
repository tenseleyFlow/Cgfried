// OPT_EQ: all
// GCC passes every variably-sized aggregate through a private copy's pointer,
// independent of its runtime size. Each va_arg consumes one pointer slot,
// dereferences it, and copies the cached runtime extent.
// EXIT_CODE: 0

#include <stdarg.h>

static int check_registers(int n, void *original, ...)
{
    struct R {
        unsigned char bytes[n];
    } value;
    struct R *source = original;
    va_list ap;
    int i;

    va_start(ap, original);
    source->bytes[0] = 99;
    value = va_arg(ap, struct R);
    for (i = 0; i < n; i++)
        if (value.bytes[i] != (unsigned char)(i + 3))
            return 1;
    value.bytes[0] = 77;
    value = va_arg(ap, struct R);
    for (i = 0; i < n; i++)
        if (value.bytes[i] != (unsigned char)(200 - i))
            return 2;
    va_end(ap);
    return 0;
}

/* Eight named integer/pointer arguments exhaust the GP bank on both SysV
 * x86-64 and AAPCS64. The two invisible pointers must then occupy adjacent
 * eight-byte overflow slots, not runtime-sized inline objects. */
static int check_stack(int n, long a1, long a2, long a3, long a4, long a5,
                       long a6, long a7, ...)
{
    struct R {
        unsigned char bytes[n];
    } value;
    va_list ap;
    int i;

    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    va_start(ap, a7);
    value = va_arg(ap, struct R);
    for (i = 0; i < n; i++)
        if (value.bytes[i] != (unsigned char)(i + 3))
            return 3;
    value = va_arg(ap, struct R);
    for (i = 0; i < n; i++)
        if (value.bytes[i] != (unsigned char)(200 - i))
            return 4;
    va_end(ap);
    return 0;
}

static int run_case(int n)
{
    struct R {
        unsigned char bytes[n];
    } first, second;
    int i, rc;

    for (i = 0; i < n; i++) {
        first.bytes[i] = (unsigned char)(i + 3);
        second.bytes[i] = (unsigned char)(200 - i);
    }
    rc = check_registers(n, &first, first, second);
    if (rc != 0 || first.bytes[0] != 99)
        return rc ? rc : 5;
    for (i = 1; i < n; i++)
        if (first.bytes[i] != (unsigned char)(i + 3))
            return 6;

    first.bytes[0] = 3;
    rc = check_stack(n, 0, 0, 0, 0, 0, 0, 0, first, second);
    if (rc != 0)
        return rc;
    for (i = 0; i < n; i++)
        if (first.bytes[i] != (unsigned char)(i + 3) ||
            second.bytes[i] != (unsigned char)(200 - i))
            return 7;
    return 0;
}

int main(void)
{
    static const int sizes[] = {1, 5, 8, 9, 16, 17};
    int i;

    for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        if (run_case(sizes[i]) != 0)
            return i + 1;
    return 0;
}
