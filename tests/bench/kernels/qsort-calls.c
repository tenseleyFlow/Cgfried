// OPT_EQ: all

#include <stddef.h>
#include <stdlib.h>

#ifndef REPS
#define REPS 5000
#endif

static volatile unsigned long sink;

static int compare_uint(const void *lhs, const void *rhs)
{
    unsigned a = *(const unsigned *)lhs;
    unsigned b = *(const unsigned *)rhs;
    return (a > b) - (a < b);
}

__attribute__((noinline)) unsigned long kernel_run(void)
{
    unsigned values[64];
    unsigned long checksum = 0;
    int r;
    unsigned i;

    for (r = 0; r < REPS; ++r) {
        for (i = 0; i < 64; ++i)
            values[i] = (i * 73u + 19u) & 255u;
        qsort(values, 64, sizeof(values[0]), compare_uint);
        checksum = values[0] * 1000000ul + values[31] * 1000ul + values[63];
    }
    return checksum;
}

int main(void)
{
    unsigned long got = kernel_run();
    sink = got;
    return got != 10123238ul;
}
