// FLAGS: -S -O2 -std=gnu17
// ASM_CHECK(x86_64-linux-gnu): {{^nd_small:}}
// ASM_CHECK(x86_64-linux-gnu): movl $7,
// ASM_CHECK(x86_64-linux-gnu): {{^nd_large:}}
// ASM_CHECK(x86_64-linux-gnu): movl $300,
// ASM_CHECK(x86_64-linux-gnu): movl %edx,
// ASM_CHECK(x86_64-linux-gnu): {{^nd_variable:}}
// ASM_CHECK(x86_64-linux-gnu): movl %edx,
// x86 `N` is an unsigned-byte immediate and `d` is dx. In the combined `Nd`
// constraint GCC selects the immediate only when the expression is a constant
// in range; an out-of-range constant and a variable both use dx.
int nd_small(void)
{
    int out;

    __asm__("movl %1, %0" : "=r"(out) : "Nd"(7));
    return out;
}

int nd_large(void)
{
    int out;

    __asm__("movl %1, %0" : "=r"(out) : "Nd"(300));
    return out;
}

int nd_variable(int value)
{
    int out;

    __asm__("movl %1, %0" : "=r"(out) : "Nd"(value));
    return out;
}
