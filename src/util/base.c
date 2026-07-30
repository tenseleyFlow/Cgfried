#include "util/base.h"

#include <stdlib.h>

#include "diag.h"

void *cgf_xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p)
        CGF_ICE("out of memory allocating %zu bytes", size);
    return p;
}

void *cgf_xrealloc(void *p, size_t size)
{
    void *q = realloc(p, size ? size : 1);
    if (!q)
        CGF_ICE("out of memory reallocating to %zu bytes", size);
    return q;
}
