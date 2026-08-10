// OPT_EQ: all

#include <stdint.h>

#ifndef REPS
#define REPS 1000
#endif

static volatile uint32_t sink;

static uint32_t fib(unsigned n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

__attribute__((noinline)) uint32_t kernel_run(void)
{
    uint32_t result = 0;
    int r;
    for (r = 0; r < REPS; ++r)
        result = fib(20);
    return result;
}

int main(void)
{
    uint32_t got = kernel_run();
    sink = got;
    return got != UINT32_C(6765);
}
