// OPT_EQ: all

#include <stdint.h>

#ifndef REPS
#define REPS 100000
#endif

struct packet {
    uint64_t a, b, c, d;
    unsigned char bytes[32];
};
static volatile uint64_t sink;

__attribute__((noinline)) uint64_t kernel_run(void)
{
    struct packet src;
    struct packet dst[8];
    uint64_t sum = 0;
    unsigned i, j;
    int r;

    src.a = 11;
    src.b = 23;
    src.c = 47;
    src.d = 89;
    for (i = 0; i < 32; ++i)
        src.bytes[i] = (unsigned char)(i * 3u + 1u);
    for (r = 0; r < REPS; ++r)
        for (i = 0; i < 8; ++i)
            dst[i] = src;
    for (i = 0; i < 8; ++i) {
        sum += dst[i].a + dst[i].b + dst[i].c + dst[i].d;
        for (j = 0; j < 32; ++j)
            sum += dst[i].bytes[j];
    }
    return sum;
}

int main(void)
{
    uint64_t got = kernel_run();
    sink = got;
    return got != UINT64_C(13520);
}
