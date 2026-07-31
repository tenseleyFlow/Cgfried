// The rax/rdx fixed-reg dance survives allocation EXACTLY: dividend in
// rax, cqo (or rdx = 0 for unsigned) into rdx, idiv/div, quotient read
// from rax / remainder from rdx — real registers post-RA (Sprint 22).
// FLAGS: -emit-mir
// MIR_CHECK: rax = mov.l
// MIR_CHECK: rdx = cqo.l rax
// MIR_CHECK: rax = idiv.l rax,
// MIR_CHECK: rdx = mov.l $0
// MIR_CHECK: rdx = div.q rax,
int f(int a, int b) { return a / b; }
unsigned long g(unsigned long a, unsigned long b) { return a % b; }
