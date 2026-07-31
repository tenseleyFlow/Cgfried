// Pointer reloc with addend: .quad sym+addend, numerically spliced
// into the byte image.
// FLAGS: -S
// ASM_CHECK: .quad	g+8
int g[4];
int *p = &g[2];
