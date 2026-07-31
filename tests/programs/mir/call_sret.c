// MEMORY-class return: hidden pointer in rdi (the sret arg), callee
// echoes it in rax per psABI.
// FLAGS: -emit-mir
// MIR_CHECK: rdi = mov.q
// MIR_CHECK: call [rip @mk]
struct Big { long a, b, c, d; };
struct Big mk(void);
long f(void) { struct Big s = mk(); return s.a; }
