// FLAGS: -fsyntax-only
// ERROR_EXPECTED: cannot appear on a bit-field
struct S { _Alignas(16) int b : 3; int o; };
