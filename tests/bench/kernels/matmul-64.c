// OPT_EQ: all

#include <stdint.h>

#ifndef REPS
#define REPS 4
#endif

static volatile uint64_t sink;

__attribute__((noinline)) uint64_t kernel_run(void)
{
    static uint32_t a[64][64];
    static uint32_t b[64][64];
    static uint32_t c[64][64];
    uint64_t checksum = 0;
    int r;
    unsigned i, j, k;

    for (i = 0; i < 64; ++i)
        for (j = 0; j < 64; ++j) {
            a[i][j] = (i + 3u * j) & 15u;
            b[i][j] = (5u * i + j) & 15u;
        }
    for (r = 0; r < REPS; ++r)
        for (i = 0; i < 64; ++i)
            for (j = 0; j < 64; ++j) {
                uint32_t v = 0;
                for (k = 0; k < 64; ++k)
                    v += a[i][k] * b[k][j];
                c[i][j] = v;
            }
    for (i = 0; i < 64; ++i)
        checksum += c[i][(i * 17u) & 63u];
    return checksum;
}

int main(void)
{
    uint64_t got = kernel_run();
    sink = got;
    return got != UINT64_C(231424);
}
