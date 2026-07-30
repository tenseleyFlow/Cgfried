// FLAGS: --dump-ast
// CHECK: DECL T: int [typedef]
// CHECK: DECL f: func(int, int) ret int
// CHECK: DECL y: T
// A parameter name shadows a typedef inside the PROTOTYPE SCOPE only. So
// the second parameter of `f` is `int T` — legal, because T entered scope
// as an ordinary identifier the moment its declarator completed (C11
// 6.2.1p7) — and the moment the prototype scope closes, T is a type name
// again. Getting the scope pop wrong turns `T y;` into a syntax error.
// (Block scope is Sprint 10's; the same scope machinery is exercised here.)
typedef int T;
int f(int x, int T);
T y;
