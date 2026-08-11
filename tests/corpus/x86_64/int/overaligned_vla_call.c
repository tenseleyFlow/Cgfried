// OPT_EQ: all
// A dynamic 64-aligned object coexists with ordinary/fixed frame objects,
// survives a call with stack-passed arguments, and is unwound at block exit.
// The callee observes the architectural stack pointer after its standard
// prologue: it must be 16-aligned, proving the caller retained the ABI's
// call-site invariant on both backends.
static int sum8(int a, int b, int c, int d, int e, int f, int g, int h)
{
    unsigned long sp;

#if defined(__x86_64__)
    __asm__ volatile("movq %%rsp, %0" : "=r"(sp));
#elif defined(__aarch64__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#else
#error "no stack-pointer spelling for this target"
#endif
    if ((sp & 15u) != 0)
        return -10000;
    return a + b + c + d + e + f + g + h;
}

static int (*volatile sum8_call)(int, int, int, int, int, int, int, int) = sum8;

static int probe(int n)
{
    int ordinary = 3;
    _Alignas(64) int fixed = 5;
    int round, total = 0;

    for (round = 0; round < 4; round++) {
        _Alignas(64) int values[n + round];
        int i;

        if (((unsigned long)values & 63u) != 0)
            return -1;
        for (i = 0; i < 8; i++)
            values[i] = round * 10 + i;
        total += sum8_call(values[0], values[1], values[2], values[3],
                           values[4], values[5], values[6], values[7]);
        if (values[0] != round * 10 || values[7] != round * 10 + 7)
            return -2; /* outgoing stack arguments overlapped the VLA */
    }
    if (((unsigned long)&fixed & 63u) != 0 || fixed != 5 || ordinary != 3)
        return -3;
    return total + fixed + ordinary;
}

int main(void)
{
    /* Four sums: 28, 108, 188, 268; plus fixed+ordinary = 8. */
    return probe(8) == 600 ? 0 : 1;
}
