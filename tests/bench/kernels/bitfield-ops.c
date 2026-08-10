// OPT_EQ: all

#include <stdint.h>

#ifndef REPS
#define REPS 200000
#endif

struct fields {
    unsigned char a : 3;
    unsigned char b : 5;
    unsigned char c : 4;
    unsigned char d : 4;
};
static volatile uint32_t sink;

__attribute__((noinline)) uint32_t kernel_run(void)
{
    struct fields f;
    uint32_t sum = 0;
    unsigned i;
    int r;

    for (r = 0; r < REPS; ++r) {
        sum = 0;
        for (i = 0; i < 256; ++i) {
            f.a = i;
            f.b = i * 3u;
            f.c = i * 5u;
            f.d = i * 7u;
            sum += f.a + f.b + f.c + f.d;
        }
    }
    return sum;
}

int main(void)
{
    uint32_t got = kernel_run();
    sink = got;
    return got != UINT32_C(8704);
}
