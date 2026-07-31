// The volatile law: each C-level volatile access is EXACTLY one flagged
// op. 3 reads + 2 writes = 5 volatile ops; the bitfield RMW is 2 more
// (whole-unit load AND store both carry volatile).
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: load i32, @v, align 4, volatile
// IR_CHECK: store i32
// IR_CHECK-NOT: load i32, @nv, align 4, volatile
volatile int v;
int nv;
struct B { volatile unsigned f : 3; } vb;
int f(void) {
    int a = v + v + v;
    v = a;
    v = a + 1;
    a += nv;
    vb.f = 2;
    return a;
}
