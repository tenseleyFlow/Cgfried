#include "unit.h"

#include "../bench/timeit.h"

void test_timeit_median_odd_even(TestCtx *t)
{
    const double odd[] = {9.0, 1.0, 5.0};
    const double even[] = {12.0, 1.0, 8.0, 3.0};

    T_ASSERT(t, cgf_timeit_median(odd, 3) == 5.0);
    T_ASSERT(t, cgf_timeit_median(even, 4) == 5.5);
    T_ASSERT(t, odd[0] == 9.0 && odd[1] == 1.0 && odd[2] == 5.0);
}

void test_timeit_mad_odd_even(TestCtx *t)
{
    const double odd[] = {1.0, 5.0, 9.0};
    const double even[] = {12.0, 1.0, 8.0, 3.0};

    T_ASSERT(t, cgf_timeit_mad(odd, 3) == 4.0);
    T_ASSERT(t, cgf_timeit_mad(even, 4) == 3.5);
}

void test_timeit_invalid_samples_fail_closed(TestCtx *t)
{
    T_ASSERT(t, cgf_timeit_median(NULL, 1) < 0.0);
    T_ASSERT(t, cgf_timeit_median(NULL, 0) < 0.0);
    T_ASSERT(t, cgf_timeit_mad(NULL, 1) < 0.0);
    T_ASSERT(t, cgf_timeit_mad(NULL, 0) < 0.0);
}

void test_timeit_maxrss_normalization(TestCtx *t)
{
    T_ASSERT_EQ_INT(t, cgf_timeit_maxrss_kb(4096, 1), 4);
    T_ASSERT_EQ_INT(t, cgf_timeit_maxrss_kb(4096, 0), 4096);
}
