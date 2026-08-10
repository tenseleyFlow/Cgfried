// OPT_EQ: all

#include <stdio.h>
#include <string.h>

#ifndef REPS
#define REPS 100000
#endif

static volatile unsigned long sink;

__attribute__((noinline)) unsigned long kernel_run(void)
{
    char buffer[64];
    int n = 0;
    int r;
    for (r = 0; r < REPS; ++r)
        n = snprintf(buffer, sizeof(buffer), "cgf:%08x:%u", 0x1234abcdu,
                     98765u);
    return (unsigned long)n * 257ul + (unsigned char)buffer[0] +
           (unsigned char)buffer[(unsigned)n - 1u];
}

int main(void)
{
    unsigned long got = kernel_run();
    sink = got;
    return got != 4778ul;
}
