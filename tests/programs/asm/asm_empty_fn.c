// The degenerate function: frame bracket and ret, nothing else.
// FLAGS: -S
// ASM_CHECK: pushq	%rbp
// ASM_CHECK: popq	%rbp
// ASM_CHECK: ret
void e(void) {}
