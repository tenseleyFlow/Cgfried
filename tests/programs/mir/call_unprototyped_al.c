// SysV x86-64 requires AL to carry the number of used XMM argument registers
// for a call without a prototype, even though such a call is not variadic.
// FLAGS: --target=x86_64-linux-gnu -emit-mir
// MIR_CHECK: rax = mov.l $1
// MIR_CHECK: call [rip @sink]
extern void sink();

void caller(void) { sink(1.0); }
