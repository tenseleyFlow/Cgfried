#ifndef CGF_UTIL_VEC_H
#define CGF_UTIL_VEC_H

#include <stdlib.h>

#include "util/base.h"

/* Type-safe dynamic array: VEC_DECL(VecInt, int); declares `VecInt` plus
 * VecInt_reserve / VecInt_push / VecInt_free, all operating on `int *` only.
 * There is deliberately no generic push helper punning through `void **`:
 * that pattern violates strict aliasing and an optimizing build will
 * miscompile it. Every function this macro emits touches only T *.
 * Zero-initialize ( = {0} ) before first use. */
#define VEC_DECL(Name, T)                                                     \
    typedef struct {                                                          \
        T *data;                                                              \
        size_t len;                                                           \
        size_t cap;                                                           \
    } Name;                                                                   \
    static inline void Name##_reserve(Name *v, size_t need)                   \
    {                                                                         \
        size_t cap;                                                           \
        if (v->cap >= need)                                                   \
            return;                                                           \
        cap = v->cap ? v->cap : 8;                                            \
        while (cap < need)                                                    \
            cap *= 2;                                                         \
        v->data = cgf_xrealloc(v->data, cap * sizeof(T));                     \
        v->cap = cap;                                                         \
    }                                                                         \
    static inline void Name##_push(Name *v, T val)                            \
    {                                                                         \
        Name##_reserve(v, v->len + 1);                                        \
        v->data[v->len++] = val;                                              \
    }                                                                         \
    static inline void Name##_free(Name *v)                                   \
    {                                                                         \
        free(v->data);                                                        \
        v->data = NULL;                                                       \
        v->len = 0;                                                           \
        v->cap = 0;                                                           \
    }                                                                         \
    typedef int Name##_require_semicolon

#endif
