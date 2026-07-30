// DoD 3: the bitfield RMW sequences. Unsigned read is shl+lshr; signed
// read is shl+ashr (sign extension for free); write is
// load / and-clear / and-mask / shl / or / store.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: shl i32
// IR_CHECK: lshr i32
// IR_CHECK: ashr i32
// IR_CHECK: and i32
// IR_CHECK: or i32
struct B { unsigned u : 3; int s : 5; unsigned pad : 1; int wide : 31; };
struct B g;
int rd(void) { return g.u + g.s + g.wide; }
void wr(int v) { g.u = v; g.s = v; }
