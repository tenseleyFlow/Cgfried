// FLAGS: -std=gnu17
// OPT_EQ: all
// EXIT_CODE: 0
/* GNU local register variables only promise their named register when used
 * as direct extended-asm operands. musl's syscall_arch.h depends on the full
 * shape below: seven simultaneously constrained values on x86-64 and the
 * syscall number/result plus six arguments on arm64. Keeping that pressure
 * here prevents an ordinary unconstrained allocation from accidentally
 * choosing one expected register and masking a lost declaration binding.
 * The native/QEMU ARM corpus also runs this under CGF_SPILL_ALL, pinning the
 * accepted-register path against spill-rewrite pressure. */
static long syscall_shape(long n, long a, long b, long c, long d, long e,
                          long f)
{
#if defined(__x86_64__)
    unsigned long out;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;

    __asm__ volatile("addq %%r10, %%rax\n\taddq %%r8, %%rax\n\taddq %%r9, %%rax"
                     : "=a"(out)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8),
                       "r"(r9)
                     : "rcx", "r11", "memory");
    return (long)out + a + b + c;
#elif defined(__aarch64__)
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;

    __asm__ volatile("add x0, x8, x0\n\tadd x0, x0, x1\n\tadd x0, x0, x2\n\t"
                     "add x0, x0, x3\n\tadd x0, x0, x4\n\tadd x0, x0, x5"
                     : "=r"(x0)
                     : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4),
                       "r"(x5)
                     : "memory", "cc");
    return x0;
#else
#error "no local-register asm fixture for this target"
#endif
}

#if defined(__x86_64__)
static long explicit_constraint_wins(void)
{
    register long r10 __asm__("r10") = 11;
    unsigned long out;

    /* The matching constraint names operand zero's rax location.  The local
     * register declaration must not override that explicit asm constraint. */
    __asm__ volatile("addq $1, %0" : "=a"(out) : "0"(r10));
    return (long)out;
}
#endif

int main(void)
{
    if (syscall_shape(1, 2, 3, 4, 5, 6, 7) != 28)
        return 1;
#if defined(__x86_64__)
    if (explicit_constraint_wins() != 12)
        return 2;
#endif
    return 0;
}
