// OPT_EQ: all

#include <stddef.h>
#include <stdint.h>

#ifndef REPS
#define REPS 100000
#endif

static volatile uint64_t sink;

__attribute__((noinline)) uint64_t kernel_run(void)
{
    uint64_t checksum = 0;
    int r;
    for (r = 0; r < REPS; ++r) {
        unsigned size = 32u + ((unsigned)r & 31u);
        unsigned char arena[size];
        unsigned i;
        checksum = 0;
        for (i = 0; i < size; ++i) {
            arena[i] = (unsigned char)(i * 9u + 7u);
            checksum += arena[i];
        }
    }
    return checksum;
}

int main(void)
{
    unsigned long last_size = 32u + ((REPS - 1u) & 31u);
    unsigned long i;
    uint64_t expected = 0;
    uint64_t got = kernel_run();
    for (i = 0; i < last_size; ++i)
        expected += (unsigned char)(i * 9u + 7u);
    sink = got;
    return got != expected;
}
