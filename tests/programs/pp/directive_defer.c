// FLAGS: -E -std=gnu17
// CHECK: f(1,2)
// CHECK: g(9, 8,7)
// CHECK: "a, b"
// CHECK: h()
// CHECK: args 4
#define M(args...) f(args)
M(1,2)
#define N(x, rest...) g(x, rest)
N(9,8,7)
#define S(rest...) #rest
S(a, b)
#define Z(rest...) h(rest)
Z()
/* In a standard variadic macro, `args` remains an ordinary body token. */
#define O(...) args __VA_ARGS__
O(4)
