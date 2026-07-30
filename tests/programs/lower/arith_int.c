// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: iadd i32
// IR_CHECK: imul i32
// IR_CHECK: isub i32
// IR_CHECK: sdiv i32
// IR_CHECK: srem i32
// IR_CHECK: udiv i32
// IR_CHECK: urem i32
int s(int a, int b) { return a + b - a * b + a / b + a % b; }
unsigned u(unsigned a, unsigned b) { return a / b + a % b; }
