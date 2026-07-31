/* cgfried freestanding <stddef.h> (C17 7.19). Types come from the
 * predefined __*_TYPE__ macros so one header stays target-true. */
#ifndef _CGF_STDDEF_H
#define _CGF_STDDEF_H

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif

/* Widest fundamental alignment — C11 and later only (_Alignas is not a
 * keyword before then, and the type is not required). Spelled over BOTH
 * candidates rather than assuming which wins: on arm64-macos long
 * double IS double, so naming just one would stop documenting intent. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
typedef struct {
    _Alignas(long long) long long __cgf_ll;
    _Alignas(long double) long double __cgf_ld;
} max_align_t;
#endif

#define NULL ((void *)0)
/* __builtin_offsetof folds in the Sprint 15 constant engine, so the
 * result is a true ICE — the &((T*)0)->m trick is not. */
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
