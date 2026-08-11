#ifndef CGF_BENCH_ARM64_MACOS_SYNTAX_H
#define CGF_BENCH_ARM64_MACOS_SYNTAX_H

/*
 * Benchmark-only compatibility for parsing SQLite against the current Apple
 * SDK.  The measured lane is syntax-only: none of these declarations can
 * affect generated code or runtime behavior.
 *
 * TargetConditionals.h documents TARGET_CPU_* as its short-term escape for an
 * unknown compiler.  SQLite documents SQLITE_WITHOUT_ZONEMALLOC as the path
 * that avoids Apple's extension-bearing zone allocator declarations.  The
 * SDK's arm thread-state declarations use __uint128_t, which is outside the
 * Cgfried v0.1 language surface, so give that unused field a layout-compatible
 * aggregate spelling (this is deliberately not an integer-128 declaration).
 * Finally, the SDK math header requires Clang-only builtins and _Float16;
 * SQLite needs only the ordinary fabs declaration in this lane.
 */
#define TARGET_CPU_ARM64 1
#define SQLITE_WITHOUT_ZONEMALLOC 1
typedef struct {
    _Alignas(16) unsigned long long lo;
    unsigned long long hi;
} __cgf_bench_u128;
#define __uint128_t __cgf_bench_u128

#define __MATH_H__ 1
extern double fabs(double);

#endif
