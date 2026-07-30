// FLAGS: -fdump-sema
// CHECK: EXPR (p = (icast<int *> a)) : int *
// CHECK: EXPR (q = (& a)) : int [4] *
// CHECK: EXPR (i = (icast<int> (sizeof a))) : int
// CHECK: EXPR (fp = (icast<int (int) *> g)) : int (int) *
// `&arr` is a pointer to the ARRAY, not to its first element — the most
// common confusion in this area, and the reason `&` suppresses decay.
// sizeof suppresses it too, which is why `sizeof a` is the array's size
// rather than a pointer's.
int g(int);
void f(void) {
    int a[4]; int *p; int (*q)[4]; int i; int (*fp)(int);
    p = a;
    q = &a;
    i = sizeof a;
    fp = g;
}
