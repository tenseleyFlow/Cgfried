// FLAGS: --dump-ast
// CHECK: EXPR (a = (b + (c * d)))
// CHECK: EXPR (a = ((b + c) * d))
// CHECK: EXPR (a = (b ? c : (d ? a : b)))
// CHECK: EXPR (a = (b = c))
// CHECK: EXPR ((a , b) , c)
// CHECK: EXPR (a = (b || (c && d)))
// CHECK: EXPR (a = (b | (c ^ (d & a))))
// CHECK: EXPR (a = ((b << c) < d))
// The dump is FULLY PARENTHESIZED on purpose: a wrong binding power shows
// up as a differently shaped tree rather than as identical-looking output.
int a, b, c, d;
void f(void) {
    a = b + c * d;
    a = (b + c) * d;
    a = b ? c : d ? a : b;
    a = b = c;
    a, b, c;
    a = b || c && d;
    a = b | c ^ d & a;
    a = b << c < d;
}
