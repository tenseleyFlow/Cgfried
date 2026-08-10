// OPT_EQ: all

#include <stdarg.h>
#include <stdint.h>

#ifndef REPS
#define REPS 200000
#endif

static volatile uint64_t sink;

static uint64_t add_many(unsigned count, ...)
{
    va_list ap;
    uint64_t sum = 0;
    unsigned i;
    va_start(ap, count);
    for (i = 0; i < count; ++i)
        sum += va_arg(ap, unsigned);
    va_end(ap);
    return sum;
}

__attribute__((noinline)) uint64_t kernel_run(void)
{
    uint64_t result = 0;
    int r;
    for (r = 0; r < REPS; ++r)
        result = add_many(8, 3u, 5u, 7u, 11u, 13u, 17u, 19u, 23u);
    return result;
}

int main(void)
{
    uint64_t got = kernel_run();
    sink = got;
    return got != UINT64_C(98);
}
