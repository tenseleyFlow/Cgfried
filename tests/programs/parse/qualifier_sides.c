// FLAGS: --dump-ast
// CHECK: DECL a: ptr to const char
// CHECK: DECL b: const ptr to char
// CHECK: DECL c: const ptr to const char
// CHECK: DECL d: restrict ptr to int
// CHECK: DECL e: ptr to volatile ptr to int
// A qualifier BEFORE the '*' belongs to the pointee; one AFTER belongs to
// the pointer itself. Getting this backwards is the classic const-correctness
// bug, and it is invisible unless the dump distinguishes the two sides.
const char *a;
char *const b;
const char *const c;
int *restrict d;
int *volatile *e;
