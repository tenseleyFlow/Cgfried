// Every RMW mapping pinned: add/sub/and/or/xor direct; mul via the
// cmpxchg retry loop; float += through the integer container.
// FLAGS: -emit-ir
// ENV: CGF_VERIFY_AFTER_EACH=1
// IR_CHECK: atomicrmw add i32 @a
// IR_CHECK: atomicrmw sub i32 @a
// IR_CHECK: atomicrmw and i32 @a
// IR_CHECK: atomicrmw or i32 @a
// IR_CHECK: atomicrmw xor i32 @a
// IR_CHECK: rmw.retry
// IR_CHECK: cmpxchg i32 @a
// IR_CHECK: cmpxchg i64 @d
_Atomic int a;
_Atomic double d;
void f(int v) {
    a += v; a -= v; a &= v; a |= v; a ^= v;
    a *= v;
    d += 1.5;
}
