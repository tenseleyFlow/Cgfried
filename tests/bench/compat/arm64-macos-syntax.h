#ifndef CGF_BENCH_ARM64_MACOS_SYNTAX_H
#define CGF_BENCH_ARM64_MACOS_SYNTAX_H

/*
 * Benchmark-only compatibility for parsing SQLite against the current Apple
 * SDK.  The measured lane is syntax-only: none of these declarations can
 * affect generated code or runtime behavior.
 *
 * TargetConditionals.h documents TARGET_CPU_* as its short-term escape for an
 * unknown compiler.  SQLite documents SQLITE_WITHOUT_ZONEMALLOC as the path
 * that avoids Apple's extension-bearing zone allocator declarations. The SDK
 * math header requires Clang-only builtins and _Float16; SQLite needs
 * only the ordinary fabs declaration in this lane. Cgfried now provides the
 * SDK's native __uint128_t spelling directly, so no layout-only substitute is
 * needed for arm thread-state declarations.
 */
#define TARGET_CPU_ARM64 1
#define SQLITE_WITHOUT_ZONEMALLOC 1
#define __MATH_H__ 1
extern double fabs(double);

#endif
