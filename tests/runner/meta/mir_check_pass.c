// FLAGS: -emit-mir
// MIR_CHECK: mir @f
// MIR_CHECK: rax = mov.l $1
int f(void) { return 1; }
