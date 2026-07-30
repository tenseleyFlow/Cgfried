// FLAGS: --dump-ast
// CHECK: DECL T: int [typedef]
// CHECK: DECL a: T
// CHECK: DECL b: int
// CHECK: DECL f: func(T) ret int
// CHECK: DECL p: ptr to T
// `T(a);` is a DECLARATION of `a`, not a call — the redundant parentheses
// around a declarator are legal. `int(b);` is the same shape with a builtin
// specifier. And `int f(x)` is a PROTOTYPE precisely because x is a visible
// typedef; had it not been, this would be a K&R identifier list (and an
// error outside a definition). Every one of these hinges on the parser's
// scope stack, not on the grammar alone.
typedef int T;
T(a);
int(b);
int f(T);
T *p;
