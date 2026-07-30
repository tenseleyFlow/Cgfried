/* cgfried freestanding <stddef.h> — minimal subset; Sprint 28 completes it
 * (offsetof switches to __builtin_offsetof, max_align_t lands). Types come
 * from the predefined __*_TYPE__ macros so they stay target-true. */
#ifndef _CGF_STDDEF_H
#define _CGF_STDDEF_H

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif

#define NULL ((void *)0)
#define offsetof(type, member) ((size_t) & ((type *)0)->member)

#endif
