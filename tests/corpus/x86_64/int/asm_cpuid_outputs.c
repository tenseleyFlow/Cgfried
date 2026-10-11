// ENV: CGF_AS=0
// OPT_EQ: all
// EXIT_CODE: 0
/* CPUID leaf zero gives four write-only fixed-register outputs and no tied
 * inputs. Besides exercising three post-asm captures, the `=b` output proves
 * the pre-asm reservation remains visible to frame finalization: rbx is
 * callee-saved and must be preserved for this function's caller. The bundled
 * assembler does not yet encode cpuid, so this executable fixture explicitly
 * uses the system assembler while the unit test inspects the backend MIR. */
static int cpuid_leaf_zero(void)
{
#if defined(__x86_64__)
    unsigned max_leaf;
    unsigned ebx;
    unsigned ecx;
    unsigned edx;

    __asm__("cpuid" : "=a"(max_leaf), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    return max_leaf >= 1 && (ebx | ecx | edx) != 0;
#elif defined(__aarch64__)
    /* The shared corpus also runs through the ARM64 cross lane. */
    return 1;
#else
#error "no test implementation for this target"
#endif
}

int main(void)
{
    return cpuid_leaf_zero() ? 0 : 1;
}
