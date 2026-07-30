// No typed GEP: a[i] is ptradd(a, i * sizeof(elt)) with a REAL imul.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: imul i64
// IR_CHECK: ptradd
int g[10];
int f(long i) { return g[i]; }
int m(int i, int j) { static int t[3][4]; return t[i][j]; }
