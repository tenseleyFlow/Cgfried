#include "util/buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"

void buf_init(Buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_free(Buf *b)
{
    free(b->data);
    buf_init(b);
}

void buf_reserve(Buf *b, size_t need)
{
    size_t cap;

    if (b->cap >= need)
        return;
    cap = b->cap ? b->cap : 64;
    while (cap < need)
        cap *= 2;
    b->data = cgf_xrealloc(b->data, cap);
    b->cap = cap;
}

void buf_append(Buf *b, const void *data, size_t len)
{
    if (len == 0)
        return; /* also dodges memcpy-from-NULL UB */
    buf_reserve(b, b->len + len);
    memcpy(b->data + b->len, data, len);
    b->len += len;
}

void buf_push_u8(Buf *b, u8 byte)
{
    buf_reserve(b, b->len + 1);
    b->data[b->len++] = byte;
}

void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap, ap2;
    int need;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        CGF_ICE("buf_printf: vsnprintf failed on format \"%s\"", fmt);
    buf_reserve(b, b->len + (size_t)need + 1);
    vsnprintf((char *)b->data + b->len, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)need; /* the NUL is scratch, not content */
}
