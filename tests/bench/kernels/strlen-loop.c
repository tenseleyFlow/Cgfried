// OPT_EQ: all

#include <stddef.h>
#include <string.h>

#ifndef REPS
#define REPS 100000
#endif

static volatile size_t sink;

__attribute__((noinline)) size_t kernel_run(void)
{
    static const char text[] =
        "cgfried-codegen-kernel-abcdefghijklmnopqrstuvwxyz-0123456789";
    size_t result = 0;
    int r;

    for (r = 0; r < REPS; ++r)
        result = strlen(text);
    return result;
}

int main(void)
{
    size_t got = kernel_run();
    sink = got;
    return got !=
           sizeof(
               "cgfried-codegen-kernel-abcdefghijklmnopqrstuvwxyz-0123456789") -
               1;
}
