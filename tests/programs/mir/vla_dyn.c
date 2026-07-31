// Dynamic alloca in a LOOP: the size rounds to 16 (add 15 / and -16),
// rsp drops by the rounded size, the block pointer is rsp itself, and
// the loop-scope exit RESTORES rsp from the stacksave — without it the
// stack leaks a VLA per iteration. Sprint 22 expands all three markers.
// FLAGS: -emit-mir
// MIR_CHECK: = mov.q rsp
// MIR_CHECK: = add.q r10, $15
// MIR_CHECK: = and.q r10, $-16
// MIR_CHECK: rsp = sub.q rsp, r10
// MIR_CHECK: = mov.q rsp
// MIR_CHECK: rsp = mov.q r
int f(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) { int a[n]; a[0] = i; s += a[0]; }
    return s;
}
