// OPT_EQ: all
// GNU argument packs are specialized before the destination call's ABI is
// planned. This crosses GP/FP limits, forwards one side-effecting pack twice,
// handles the empty pack and reclassifies an aggregate from registers at the
// wrapper boundary to the stack at the inner call.
// EXIT_CODE: 0
#include <stdarg.h>

struct Pair {
    int i;
    double d;
};

static int mixed_sink(int n, ...)
{
    va_list ap;
    int i;
    int sum = 0;

    va_start(ap, n);
    for (i = 0; i < n; i += 2) {
        sum += va_arg(ap, int);
        sum += (int)va_arg(ap, double);
    }
    va_end(ap);
    return sum;
}

static inline int forward_mixed(int n, ...)
{
    return mixed_sink(n, __builtin_va_arg_pack());
}

static int int_sink(int n, ...)
{
    va_list ap;
    int i;
    int sum = 0;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        sum += va_arg(ap, int);
    va_end(ap);
    return sum;
}

static inline int forward_twice(int n, ...)
{
    return int_sink(n, __builtin_va_arg_pack()) +
           int_sink(n, __builtin_va_arg_pack());
}

static inline int forwarded_count(int marker, ...)
{
    (void)marker;
    return __builtin_va_arg_pack_len();
}

static int pair_sink(int a, int b, int c, int d, int e, int n, ...)
{
    va_list ap;
    struct Pair p;

    va_start(ap, n);
    p = va_arg(ap, struct Pair);
    va_end(ap);
    return a + b + c + d + e + n + p.i + (int)p.d;
}

static inline int forward_pair(int n, ...)
{
    return pair_sink(1, 2, 3, 4, 5, n, __builtin_va_arg_pack());
}

static int many_pair_sink(int n, ...)
{
    va_list ap;
    int i;
    int sum = 0;

    va_start(ap, n);
    for (i = 0; i < n; i++) {
        struct Pair p = va_arg(ap, struct Pair);

        sum += p.i + (int)p.d;
    }
    va_end(ap);
    return sum;
}

static inline int forward_many_pairs(int n, ...)
{
    return many_pair_sink(n, __builtin_va_arg_pack());
}

/* Seventy 16-byte aggregates become 140 AAPCS64 IR operands once the GP
 * argument registers are exhausted. This is intentionally beyond the old
 * fixed 130-operand scratch buffer. */
#define TEN_PAIRS(x) x, x, x, x, x, x, x, x, x, x
#define SEVENTY_PAIRS(x)                                                       \
    TEN_PAIRS(x), TEN_PAIRS(x), TEN_PAIRS(x), TEN_PAIRS(x), TEN_PAIRS(x),      \
        TEN_PAIRS(x), TEN_PAIRS(x)

static int effects;

static int next(void)
{
    effects++;
    return effects;
}

int main(void)
{
    struct Pair p = {7, 8.0};

    if (forward_mixed(20, 1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, 9, 10.0, 11, 12.0, 13,
                      14.0, 15, 16.0, 17, 18.0, 19, 20.0) != 210)
        return 1;
    if (forward_twice(2, next(), next()) != 6 || effects != 2)
        return 2;
    if (forwarded_count(0) != 0 || forwarded_count(0, 1, 2, 3) != 3)
        return 3;
    if (forward_pair(6, p) != 36)
        return 4;
    if (forward_many_pairs(70, SEVENTY_PAIRS(p)) != 1050)
        return 5;
    return 0;
}
