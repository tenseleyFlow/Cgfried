#include "timeit.h"

#include <stdio.h>

static int check(int condition, const char *message)
{
    if (condition)
        return 1;
    fprintf(stderr, "timeit_math_test: %s\n", message);
    return 0;
}

int main(void)
{
    const double odd[] = {9.0, 1.0, 5.0};
    const double even[] = {12.0, 1.0, 8.0, 3.0};
    int ok = 1;

    ok &= check(cgf_timeit_median(odd, 3) == 5.0, "odd median is incorrect");
    ok &= check(cgf_timeit_median(even, 4) == 5.5, "even median is incorrect");
    ok &= check(cgf_timeit_mad(odd, 3) == 4.0, "odd MAD is incorrect");
    ok &= check(cgf_timeit_mad(even, 4) == 3.5, "even MAD is incorrect");
    ok &= check(cgf_timeit_median(NULL, 0) < 0.0,
                "empty median did not fail closed");
    ok &= check(cgf_timeit_mad(NULL, 0) < 0.0, "empty MAD did not fail closed");
    ok &= check(cgf_timeit_maxrss_kb(4096, 1) == 4,
                "macOS RSS bytes were not converted to KiB");
    ok &= check(cgf_timeit_maxrss_kb(4096, 0) == 4096,
                "Linux RSS KiB were converted twice");

    if (!ok)
        return 1;
    puts("timeit_math_test: median, MAD, and RSS normalization passed");
    return 0;
}
