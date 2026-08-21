// FLAGS: --dump-ast
// CHECK: DECL unnamed: func(int, ...) ret int
// CHECK: DECL named: func(int, ...) ret int
// CHECK: DECL pointer: ptr to func(int) ret int
// CHECK: FUNCDEF old: func(K&R:value) ret int
// FE-H-01/02 boundary controls: an unnamed typed parameter may precede the
// ellipsis, typed nested prototypes remain valid, and an outer K&R function
// definition remains valid.
int unnamed(int, ...);
int named(int value, ...);
int (*pointer)(int);

int old(value)
int value;
{
    return value;
}
