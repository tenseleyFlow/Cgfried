// FLAGS: --target=x86_64-linux-gnu -S -std=gnu17
// ASM_CHECK: cpuid
// ASM_CHECK: movl %ecx
// ASM_CHECK: movl %edx
/* Write-only fixed extra outputs are reserved at the asm point even though
 * no input interval names their physical registers. This is zstd's leaf-one
 * CPU-feature probe. */
void cpuid_leaf_one(unsigned *pa, unsigned *pc, unsigned *pd)
{
    unsigned a;
    unsigned c;
    unsigned d;

    __asm__("cpuid" : "=a"(a), "=c"(c), "=d"(d) : "a"(1) : "ebx");
    *pa = a;
    *pc = c;
    *pd = d;
}
