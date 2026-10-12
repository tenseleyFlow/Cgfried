# Changelog

All notable changes to Cgfried are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-10-12

### Added

- ISO C17 compilation with C89/C90, C99, C11, C17/C18, and corresponding GNU
  language modes; deterministic preprocessing and diagnostics.
- Native and cross code generation for `x86_64-linux-gnu`, `arm64-linux`,
  `arm64-macos`, `x86_64-linux-musl`, and `x86_64-freebsd`.
- Multi-translation-unit compilation, dependency generation, system or
  bundled assembler/linker routing, freestanding headers, and `libcgf_rt`.
- GCC 8-class `-O2` corpus equivalence gates, architecture-specific optimizer
  and code-quality ratchets, and reproducible performance reports.
- `-Wall -Wextra` differential coverage plus configurable warning groups,
  promotion, pragmas, parseable fix-its, and bounded automatic repair.
- Memory diagnostics, `-fcgf-safe` runtime instrumentation, and the `-fsafe`
  safe-translation-unit policy with mixed-object interoperability checks.
- Deterministic two-stage self-hosting with a completed 34-day hosted soak,
  five-target CI, sanitizers, fuzzing, ABI differentials, and FOSS campaigns.

[Unreleased]: https://github.com/tenseleyFlow/Cgfried/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/tenseleyFlow/Cgfried/releases/tag/v0.1.0
