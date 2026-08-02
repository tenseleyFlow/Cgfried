// FLAGS: -fdump-init
// WARN_CHECK: initializer-string-too-long initializer-string for array of chars is too long
char initializer_string_too_long[2] = "long";
