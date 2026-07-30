// !x is one icmp eq; !!x is two (no fused truthiness); fp !x is oeq 0.0
// (NaN is truthy, so !NaN is 0).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: icmp eq i32
// IR_CHECK: fcmp oeq f64
int f(int a, double d) { return !a + !!a + !d; }
