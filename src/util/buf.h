#ifndef CGF_UTIL_BUF_H
#define CGF_UTIL_BUF_H

#include <stddef.h>

#include "util/base.h"

/* Growable byte buffer — the future assembly/object writer's substrate. */
typedef struct {
    u8 *data;
    size_t len;
    size_t cap;
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_reserve(Buf *b, size_t need);
void buf_append(Buf *b, const void *data, size_t len);
void buf_push_u8(Buf *b, u8 byte);
void buf_printf(Buf *b, const char *fmt, ...);

#endif
