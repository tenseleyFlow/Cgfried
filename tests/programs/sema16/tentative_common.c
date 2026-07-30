// FLAGS: -fdump-sema
// CHECK: var t1: int [external] [tentative] [common]
// CHECK: var t2: int [external] [tentative] [common]
// CHECK: var st: int [internal] [tentative] [zero-init]
// CHECK: var def: int [external] [defined]
// CHECK: var arr: int [1] [external] [tentative] [common]
// WARNING_EXPECTED: array 'arr' assumed to have one element
// gcc 8's default is -fcommon: EXTERNAL tentatives become COMMON symbols
// (so several TUs each saying `int t1;` still link), while internal ones
// resolve to zero-initialized definitions. `int arr[];` completes to one
// element at end of TU, warning included — all gcc's behavior.
int t1;
int t1;
int t2;
static int st;
int def = 7;
int arr[];
