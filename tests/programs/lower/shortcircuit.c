// DoD 4: short-circuit lowering emits ZERO allocas — the result travels
// as a block parameter. (Globals, not params: parameters spill.)
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: and.rhs
// IR_CHECK: and.join
// IR_CHECK: or.rhs
// IR_CHECK: or.join
// IR_CHECK-NOT: alloca
int g1, g2, g3;
int f(void) { return (g1 && g2) || g3; }
int h(void) { return g1 && (g2 || !g3); }
