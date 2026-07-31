// A value live ACROSS a call cannot keep a caller-saved register: the
// allocator must give it a callee-saved one (or spill).
// FLAGS: -emit-mir
// MIR_CHECK: push.q rbx
// MIR_CHECK: call [rip @get]
// MIR_CHECK: load.l [rbx]
int get(void);
int f(int a) { int b = get(); return a + b; }
