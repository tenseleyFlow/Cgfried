// _Atomic went LIVE in Sprint 20 (this fixture pinned the deferral):
// plain accesses are seq_cst; compound ops are RMW.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: load i32, @a, align 4, seq_cst
// IR_CHECK: atomicrmw add i32 @a
_Atomic int a;
int f(int v) { int x = a; a += v; return x; }
