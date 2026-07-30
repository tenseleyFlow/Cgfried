// FLAGS: --dump-ast
// CHECK: EMPTY_DECL enum E
// CHECK: ENUMERATOR RED
// CHECK: ENUMERATOR GREEN = [expr]
// CHECK: ENUMERATOR BLUE
// CHECK: DECL arr: array [expr] of int [init]
// CHECK: DECL p: array [expr] of struct P [init]
// CHECK: DECL s: ptr to const char [init]
// Enumerator values and array bounds are stored as EXPRESSIONS, never
// folded here — Sprint 15 evaluates them. Likewise designator chains are
// recorded verbatim; the current-object walk that gives `[2].x[1]` its
// meaning belongs to sema, not to the grammar.
enum E { RED, GREEN = 5, BLUE, };
int arr[3] = { [0] = 1, [2] = 3 };
struct P { int x[2]; };
struct P p[3] = { [2].x[1] = 7 };
const char *s = "hello";
