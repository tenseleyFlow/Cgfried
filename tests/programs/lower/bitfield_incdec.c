// ++/-- on a bitfield goes through the FULL RMW; the result is the
// re-narrowed stored value.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: lshr
// IR_CHECK: or i32
struct B { unsigned n : 4; } g;
unsigned f(void) { return g.n++ + ++g.n; }
