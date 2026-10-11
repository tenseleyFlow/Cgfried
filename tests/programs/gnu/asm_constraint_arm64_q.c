// FLAGS: --target=arm64-linux -S -O2 -std=gnu17
// ASM_CHECK(arm64-linux): {{prfm[ \t]+pldl1keep, \[x[0-9]+\]}}
/* CAMP-ZSTD-001: GNU AArch64 `Q` is a memory operand whose address is held in
 * one general register. Zstd uses this exact shape for its portable ARM64
 * prefetch macro when a compiler does not advertise GNU builtin support. */
void prefetch_l1(const char *p)
{
    __asm__ __volatile__("prfm pldl1keep, %0" : : "Q"(*p));
}
