// OPT_EQ: all

#ifndef REPS
#define REPS 200000
#endif

static volatile double sink;

__attribute__((noinline)) double kernel_run(void)
{
    static const double coeff[8] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
    double result = 0.0;
    int r, i;

    for (r = 0; r < REPS; ++r) {
        result = coeff[7];
        for (i = 6; i >= 0; --i)
            result = result * 2.0 + coeff[i];
    }
    return result;
}

int main(void)
{
    double got = kernel_run();
    sink = got;
    return got != 1793.0;
}
