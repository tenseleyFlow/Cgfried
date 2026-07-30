// FLAGS: --dump-tokens -std=c17
// CHECK: KEYWORD inline
// CHECK: KEYWORD restrict
// CHECK: KEYWORD _Bool
// CHECK: KEYWORD _Static_assert
// CHECK: IDENT typeof
// typeof is a keyword only in gnu modes.
inline restrict _Bool _Static_assert typeof
