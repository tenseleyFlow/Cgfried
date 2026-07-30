// p+n scales by sizeof(elt); p-q divides the byte difference (sdiv,
// exact); the difference has pointer-diff type (long on LP64).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: imul i64
// IR_CHECK: ptradd
// IR_CHECK: bitcast ptr
// IR_CHECK: sdiv i64
long f(int *p, int *q, int n) { return (p + n) - q; }
int *g(int *p, int n) { return p - n; }
