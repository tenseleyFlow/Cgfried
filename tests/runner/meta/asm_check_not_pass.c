// A satisfied ASM_CHECK-NOT: the text is genuinely absent, so this PASSES.
// FLAGS: -S
// ASM_CHECK: {{^f:}}
// ASM_CHECK-NOT: zzz_never_emitted
int f(void) { return 1; }
