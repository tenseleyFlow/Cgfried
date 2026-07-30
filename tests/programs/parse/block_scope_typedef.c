// FLAGS: --dump-ast
// CHECK: DECL T: int [typedef]
// CHECK: DECL T: T
// CHECK: DECL y: T
// THE pitfall Sprint 9 could not test without block scope: `T T;` declares
// a VARIABLE T, because the specifier was read while T still named the
// type and the name only becomes ordinary once ITS declarator completes
// (6.2.1p7). The closing brace undoes it, so `T y;` outside is a
// declaration again.
typedef int T;
void f(void) {
    { T T; (void)T; }
    T y;
    (void)y;
}
