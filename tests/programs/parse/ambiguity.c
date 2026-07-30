// FLAGS: --dump-ast
// CHECK: EXPR (y = (cast<T> x))
// CHECK: EXPR (y = (call g [1] x))
// CHECK: EXPR (y = (sizeof<T>))
// CHECK: EXPR (y = (sizeof x))
// CHECK: EXPR (y = (sizeof (complit<int>[1])))
// CHECK: EXPR (y = ((complit<array of int>[2])[0]))
// CHECK: EXPR (y = (cast<int> (& (complit<struct S>[1]))))
// CHECK: EXPR (y = (call g [3] x (x , y) y))
// `(T)(x)` and `(g)(x)` are the same shape and opposite meanings; the only
// thing that separates them is whether the name is a visible typedef. The
// compound-literal cases are the other half: `(T){...}` is a postfix
// expression, so it keeps taking postfix operators.
typedef int T;
struct S { int m; };
int x, y;
int g(int);
void f(void) {
    y = (T)(x);
    y = (g)(x);
    y = sizeof(T);
    y = sizeof x;
    y = sizeof (int){1};
    y = (int[]){1,2}[0];
    y = (int)&(struct S){0};
    y = g(x, (x, y), y);
}
