#ifndef CGF_BENCH_ARM64_MACOS_SELF_SYNTAX_H
#define CGF_BENCH_ARM64_MACOS_SELF_SYNTAX_H

/*
 * Benchmark-only compatibility for syntax-checking Cgfried's complete source
 * set against the current Apple SDK. Apple headers use the Clang preprocessor
 * operator __has_include and expose __uint128_t in arm thread-state
 * declarations; neither is part of Cgfried v0.1. Header substitutions that
 * require an include live in arm64-macos-self-overlay, so this forced header
 * never expands the measured include surface.
 */
#define __has_include(header) 0
typedef struct {
    _Alignas(16) unsigned long long lo;
    unsigned long long hi;
} __cgf_bench_u128;
#define __uint128_t __cgf_bench_u128

#endif
