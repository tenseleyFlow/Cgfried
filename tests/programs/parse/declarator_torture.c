// FLAGS: --dump-ast
// CHECK: DECL f: array [expr] of ptr to func(void) ret ptr to array [expr] of int
// CHECK: DECL sig: func(int, ptr to func(int) ret void) ret ptr to func(int) ret void
// CHECK: DECL fp: ptr to func(void) ret int
// CHECK: DECL g: func(void) ret ptr to int
// CHECK: DECL ap: ptr to array [expr] of int
// CHECK: DECL aa: array [expr] of array [expr] of int
// CHECK: DECL flex: array of int
// THE canonical spiral. `f` is an array of 3 pointers to functions of no
// arguments returning a pointer to an array of 5 int. The suffix binds
// TIGHTER than the prefix '*', which is why the array comes first in the
// English reading even though '*' is written first.
int (*(*f[3])(void))[5];
void (*sig(int, void (*)(int)))(int);
int (*fp)(void);
int *g(void);
int (*ap)[5];
int aa[3][4];
int flex[];
