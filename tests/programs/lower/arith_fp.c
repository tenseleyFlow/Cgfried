// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: fadd f64
// IR_CHECK: fmul f64
// IR_CHECK: fsub f64
// IR_CHECK: fdiv f64
// IR_CHECK: fneg f64
// IR_CHECK: 0x3FF0000000000000
// IR_CHECK: fadd f32 %
double d(double a, double b) { return a + b - a * b + a / b + -a + 1.0; }
float f(float a, float b) { return a + b; }
