// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: cond.then
// IR_CHECK: cond.else
// IR_CHECK: cond.join
// IR_CHECK-NOT: alloca
int g;
int f(void) { return g ? g + 1 : g - 1; }
