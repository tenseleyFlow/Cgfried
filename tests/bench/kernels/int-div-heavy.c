// OPT_EQ: all

#include <stdint.h>

#ifndef REPS
#define REPS 200000
#endif

static volatile uint32_t sink;

__attribute__((noinline)) uint32_t kernel_run(void)
{
    uint32_t sum = 0;
    uint32_t i;
    int r;

    for (r = 0; r < REPS; ++r) {
        sum = 0;
        for (i = 1; i <= 256; ++i)
            sum += i / 3u + i / 7u + i / 10u + i % 13u;
    }
    return sum;
}

int main(void)
{
    uint32_t got = kernel_run();
    sink = got;
    return got != UINT32_C(20172);
}
