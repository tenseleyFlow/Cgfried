// Variable shift count = CL fixed-reg constraint; constant rides imm.
// Post-RA the variable count physically lands in rcx.
// FLAGS: -emit-mir
// MIR_CHECK: shl.l
// MIR_CHECK: , rcx  ; 2addr
int f(int a, int n) { return (a << 3) >> n; }
