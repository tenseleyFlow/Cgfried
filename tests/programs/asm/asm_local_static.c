// Internal linkage: .local, no .globl; tentative internal lands in bss.
// FLAGS: -S
// ASM_CHECK: .globl	bump
// ASM_CHECK: .local	counter
static long counter;
long bump(void) { return ++counter; }
