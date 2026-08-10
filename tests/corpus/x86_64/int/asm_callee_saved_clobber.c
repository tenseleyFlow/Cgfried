// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 36
/* A named callee-saved clobber needs exact-register exclusion. Treating the
 * asm merely like a call protects caller-saved registers, but can attract a
 * value to rbx -- precisely the register the template destroys. Eight live
 * inputs force allocation far enough into the pool to exercise that edge;
 * CGF_SPILL_ALL=1 reruns the same proof with asm operands still no-spill. */
extern int printf(const char *, ...);

static long sum8(long a, long b, long c, long d, long e, long f, long g, long h)
{
#if defined(__x86_64__)
    long out;

    __asm__ volatile("movq $99, %%rbx\n\t"
                     "movq %1, %0\n\t"
                     "addq %2, %0\n\t"
                     "addq %3, %0\n\t"
                     "addq %4, %0\n\t"
                     "addq %5, %0\n\t"
                     "addq %6, %0\n\t"
                     "addq %7, %0\n\t"
                     "addq %8, %0"
                     : "=&r"(out)
                     : "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f), "r"(g),
                       "r"(h)
                     : "rbx");
    return out;
#elif defined(__aarch64__)
    return a + b + c + d + e + f + g + h;
#else
#error "no test implementation for this target"
#endif
}

int main(void)
{
    long value = sum8(1, 2, 3, 4, 5, 6, 7, 8);

    printf("%ld\n", value);
    return value == 36 ? 0 : 1;
}
