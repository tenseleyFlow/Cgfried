// The comma operator discards the LHS VALUE but keeps its side effects,
// in order.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: store i32
// IR_CHECK: iadd i32
int g;
int f(int a) { return (g = a, a + 1); }
