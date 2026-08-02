// FLAGS: -fdump-init
// DIVERGES(gcc-8): CGF uses its Clang-compatible warning name and dump-only driver mode.
// WARN_CHECK: initializer-string-too-long initializer-string for array of chars is too long
char initializer_string_too_long[2] = "long";
