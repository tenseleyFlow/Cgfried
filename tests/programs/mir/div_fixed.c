// The rax/rdx fixed-reg dance; test r,r replaces cmp $0.
// FLAGS: -emit-mir
// MIR_CHECK: :0 = mov.l v
// MIR_CHECK: cqo.l
// MIR_CHECK: :0 = idiv.l
// MIR_CHECK: :2 = div.q
int f(int a, int b) { return a / b; }
unsigned long g(unsigned long a, unsigned long b) { return a % b; }
