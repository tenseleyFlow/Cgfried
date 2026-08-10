// OPT_EQ: all

#ifndef REPS
#define REPS 100000
#endif

static volatile double sink;

__attribute__((noinline)) double kernel_run(void)
{
    double a[32], b[32];
    double sum = 0.0;
    unsigned i;
    int r;

    for (i = 0; i < 32; ++i) {
        a[i] = (double)(i + 1u);
        b[i] = (double)(32u - i);
    }
    for (r = 0; r < REPS; ++r) {
        sum = 0.0;
        for (i = 0; i < 32; ++i)
            sum += a[i] * b[i];
    }
    return sum;
}

int main(void)
{
    double got = kernel_run();
    sink = got;
    return got != 5984.0;
}
