// Pre yields the new value, post the old; pointers step by sizeof(elt);
// floats use fadd with an exact-bits 1.0.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: iadd i32
// IR_CHECK: ptradd
// IR_CHECK: 0x3FF0000000000000
int f(int a) { return ++a + a--; }
int *g(int *p) { return ++p; }
double h(double d) { return d++; }
