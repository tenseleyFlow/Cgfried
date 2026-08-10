// OPT_EQ: all

#include <stddef.h>
#include <stdint.h>

#ifndef REPS
#define REPS 50000
#endif

static volatile uint32_t sink;

__attribute__((noinline)) uint32_t kernel_run(void)
{
    static const unsigned char data[] = "Cgfried deterministic FNV kernel";
    uint32_t hash = 0;
    int r;
    size_t i;

    for (r = 0; r < REPS; ++r) {
        hash = UINT32_C(2166136261);
        for (i = 0; i + 1 < sizeof(data); ++i) {
            hash ^= data[i];
            hash *= UINT32_C(16777619);
        }
    }
    return hash;
}

int main(void)
{
    uint32_t got = kernel_run();
    sink = got;
    return got != UINT32_C(332450732);
}
