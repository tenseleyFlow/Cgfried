// Indirect call: the callee rides a register, args queue as usual.
// FLAGS: -emit-mir
// MIR_CHECK: call r
int f(int (*fp)(int, int)) { return fp(3, 4); }
