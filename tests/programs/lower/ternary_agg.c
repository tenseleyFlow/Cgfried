// ?: on aggregates: one temporary, both arms memcpy into it.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: cond.then
// IR_CHECK: memcpy
// IR_CHECK: cond.else
// IR_CHECK: memcpy
struct S { int a[4]; };
struct S x, y;
int pick(int c) { return (c ? x : y).a[0]; }
