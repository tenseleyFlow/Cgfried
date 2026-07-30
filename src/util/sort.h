#ifndef CGF_UTIL_SORT_H
#define CGF_UTIL_SORT_H

#include <stddef.h>

/* Stable mergesort. The libc sort is unstable, and glibc and musl disagree
 * on tie order — sorting anything that reaches output with it would make
 * builds differ across libcs and break the byte-identical stage1 == stage2
 * bootstrap (Sprint 58). The raw libc sort is therefore banned repo-wide;
 * this is the one sort. Comparator gets a context pointer so no globals. */
void cgf_sort_stable(void *base, size_t n, size_t elem_size,
                     int (*cmp)(const void *a, const void *b, void *ctx),
                     void *ctx);

#endif
