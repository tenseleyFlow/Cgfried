#ifndef CGF_TESTS_BENCH_TIMEIT_H
#define CGF_TESTS_BENCH_TIMEIT_H

#include <stddef.h>

/* These helpers do not modify their samples. They return a negative value
 * when count is zero or temporary storage cannot be allocated. */
double cgf_timeit_median(const double *samples, size_t count);
double cgf_timeit_mad(const double *samples, size_t count);

/* wait4 reports ru_maxrss in bytes on macOS and KiB on Linux. */
long cgf_timeit_maxrss_kb(long raw_maxrss, int raw_is_bytes);

#endif
