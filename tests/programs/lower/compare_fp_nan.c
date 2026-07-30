// The NaN corner: C == lowers to oeq, != to une (NaN != NaN is TRUE).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: fcmp oeq f64
// IR_CHECK: fcmp une f64
// IR_CHECK: fcmp olt f64
int f(double a, double b) { return (a == b) + (a != b) + (a < b); }
