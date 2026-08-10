// OPT_EQ: all

#include <stddef.h>

#ifndef REPS
#define REPS 2000
#endif

static volatile unsigned sink;

__attribute__((noinline)) unsigned kernel_run(void)
{
    unsigned char composite[4096];
    unsigned count = 0;
    int r;
    unsigned i, j;

    for (r = 0; r < REPS; ++r) {
        for (i = 0; i < 4096; ++i)
            composite[i] = 0;
        for (i = 2; i * i < 4096; ++i)
            if (!composite[i])
                for (j = i * i; j < 4096; j += i)
                    composite[j] = 1;
        count = 0;
        for (i = 2; i < 4096; ++i)
            count += composite[i] == 0;
    }
    return count;
}

int main(void)
{
    unsigned got = kernel_run();
    sink = got;
    return got != 564u;
}
