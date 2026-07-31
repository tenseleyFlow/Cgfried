// A 4097-byte zero tail collapses to .zero; the nonzero head stays
// numeric.
// FLAGS: -S
// ASM_CHECK: .byte	7
// ASM_CHECK: .zero	4097
char blob[4098] = {7};
