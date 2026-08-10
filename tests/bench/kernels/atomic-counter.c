// OPT_EQ: all

#ifndef REPS
#define REPS 1000000
#endif

static volatile unsigned sink;

__attribute__((noinline)) unsigned kernel_run(void)
{
    _Atomic unsigned counter = 0;
    int r;
    for (r = 0; r < REPS; ++r)
        counter++;
    return counter;
}

int main(void)
{
    unsigned got = kernel_run();
    sink = got;
    return got != (unsigned)REPS;
}
