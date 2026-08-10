// OPT_EQ: all

#include <stddef.h>
#include <string.h>

#ifndef REPS
#define REPS 20000
#endif

static volatile unsigned long sink;

__attribute__((noinline)) unsigned long kernel_run(void)
{
    unsigned char src[256];
    unsigned char dst[256];
    unsigned long sum = 0;
    int r;
    size_t i;

    for (i = 0; i < 256; ++i)
        src[i] = (unsigned char)(i * 37u + 11u);
    for (r = 0; r < REPS; ++r) {
        memcpy(dst, src, sizeof(dst));
        sum = dst[(unsigned)r & 255u];
    }
    return sum;
}

int main(void)
{
    unsigned long got = kernel_run();
    unsigned long expected = (unsigned char)(((REPS - 1u) & 255u) * 37u + 11u);
    sink = got;
    return got != expected;
}
