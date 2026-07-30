#include "util/sort.h"

#include <stdlib.h>
#include <string.h>

#include "util/base.h"

/* Bottom-up mergesort: stable because merge takes from the left run on ties.
 * O(n) scratch, O(n log n) always — no quicksort pathologies, no recursion. */

static void merge(unsigned char *dst, const unsigned char *src, size_t lo,
                  size_t mid, size_t hi, size_t esz,
                  int (*cmp)(const void *, const void *, void *), void *ctx)
{
    size_t i = lo, j = mid, k = lo;

    while (i < mid && j < hi) {
        if (cmp(src + j * esz, src + i * esz, ctx) < 0) {
            memcpy(dst + k * esz, src + j * esz, esz);
            j++;
        } else {
            /* left wins ties: stability lives on this branch */
            memcpy(dst + k * esz, src + i * esz, esz);
            i++;
        }
        k++;
    }
    if (i < mid)
        memcpy(dst + k * esz, src + i * esz, (mid - i) * esz);
    else if (j < hi)
        memcpy(dst + k * esz, src + j * esz, (hi - j) * esz);
}

void cgf_sort_stable(void *base, size_t n, size_t esz,
                     int (*cmp)(const void *, const void *, void *), void *ctx)
{
    unsigned char *a = base;
    unsigned char *tmp;
    size_t width;

    if (n < 2 || esz == 0)
        return;
    tmp = cgf_xmalloc(n * esz);

    for (width = 1; width < n; width *= 2) {
        size_t lo;

        for (lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            merge(tmp, a, lo, mid, hi, esz, cmp, ctx);
        }
        memcpy(a, tmp, n * esz);
    }
    free(tmp);
}
