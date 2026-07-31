// Two-eightbyte return: the VALUE comes back in rax:rdx; the caller
// stores the pair through the sret-shaped pointer after the call.
// FLAGS: -emit-mir
// MIR_CHECK: call [rip @mkp]
// MIR_CHECK: store.q rax, [r
// MIR_CHECK: store.q rdx, [r
struct P { long x, y; };
struct P mkp(void);
long f(void) { struct P s = mkp(); return s.x + s.y; }
