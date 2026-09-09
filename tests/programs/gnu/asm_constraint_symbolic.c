// FLAGS: -S -O2 -std=gnu17 -fno-pic -fno-pie
// ASM_CHECK(x86_64-linux-gnu): # symbolic S0=global S1=global+4 S2=.Lstr.0 S3=target I0=global I1=global+4 N=17
// ASM_CHECK(x86_64-linux-gnu): movq $global, %rax
// ASM_CHECK(arm64-linux): # symbolic S0=global S1=global+4 S2=.Lstr.0 S3=target I0=global I1=global+4 N=17
// ASM_CHECK(arm64-linux): adrp x0, global
// ASM_CHECK(arm64-macos): # symbolic S0=_global S1=_global+4 S2=l_str.0 S3=_target I0=_global I1=_global+4 N=17
// ASM_CHECK(arm64-macos): adrp x0, _global@PAGE
// GNU `s` accepts symbolic address constants but rejects numeric constants.
// GNU `i` accepts both symbolic addresses and integers, while `n` remains the
// narrower known-integer constraint.  `%c` prints a constant without the
// target's ordinary immediate prefix (`$` on x86, `#` on arm64).
int global[2];

void target(void)
{
}

void symbolic_constants(void)
{
    __asm__ volatile("# symbolic S0=%c0 S1=%c1 S2=%c2 S3=%c[function] "
                     "I0=%c4 I1=%c5 N=%c6"
                     :
                     : "s"(global), "s"(&global[1]), "s"("abc"),
                       [function] "s"(target), "i"(global), "i"(&global[1]),
                       "n"(17));
}

void symbolic_instruction(void)
{
#if defined(__aarch64__)
#if defined(__APPLE__)
    __asm__ volatile("adrp x0, %0@PAGE" : : "s"(global) : "x0");
#else
    __asm__ volatile("adrp x0, %0" : : "s"(global) : "x0");
#endif
#else
    __asm__ volatile("movq %0, %%rax" : : "s"(global) : "rax");
#endif
}
