// OPT_EQ: all

#include <stddef.h>
#include <string.h>

#ifndef REPS
#define REPS 20000
#endif

static volatile unsigned long sink;

__attribute__((noinline)) unsigned long kernel_run(void)
{
    unsigned char data[512];
    unsigned long sum = 0;
    int r;
    size_t i;

    for (r = 0; r < REPS; ++r) {
        memset(data, r & 255, sizeof(data));
        sum = 0;
        for (i = 0; i < sizeof(data); i += 31)
            sum += data[i];
    }
    return sum;
}

int main(void)
{
    unsigned long got = kernel_run();
    unsigned long expected = 17u * ((REPS - 1u) & 255u);
    sink = got;
    return got != expected;
}
