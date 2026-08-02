// FLAGS: -E -Wsystem-headers -isystem tests/programs/pp
// DIVERGES(gcc-8): CGF exposes Clang-compatible -Wnull-character in system headers.
// WARN_COUNT: 1
#include <fuzz_embedded_nul.c>
