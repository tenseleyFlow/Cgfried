// FLAGS: -fdump-sema
// CHECK: EXPR (l = (c ? (icast<long> i) : l)) : long
// CHECK: EXPR (p = (c ? p : (icast<int *> 0))) : int *
// CHECK: EXPR (vp = (c ? vp : p)) : void *
// Arithmetic operands take the usual arithmetic conversions; one side a
// null pointer constant yields the OTHER side's type; one side void *
// yields void *.
void f(void) {
    int c; int i; long l; int *p; void *vp;
    l = c ? i : l;
    p = c ? p : 0;
    vp = c ? vp : p;
}
