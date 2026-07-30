// FLAGS: --dump-tokens -std=c89
// CHECK: KEYWORD int
// CHECK: IDENT inline
// CHECK: IDENT restrict
// CHECK: IDENT _Bool
// CHECK: KEYWORD __attribute__
// In c89 these are ORDINARY IDENTIFIERS — `int inline = 1;` compiles.
// The __-spelled variants stay keywords in every mode (gcc parity).
int inline restrict _Bool __attribute__
