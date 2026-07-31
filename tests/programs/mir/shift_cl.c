// Variable shift count = CL fixed-reg constraint; constant rides imm.
// FLAGS: -emit-mir
// MIR_CHECK: shl.l v1, $3
// MIR_CHECK: :1
int f(int a, int n) { return (a << 3) >> n; }
