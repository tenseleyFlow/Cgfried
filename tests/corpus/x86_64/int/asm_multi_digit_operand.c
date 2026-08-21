// OPT_EQ: all
// EXIT_CODE: 0
/* X64-M-03: operand 10 must be parsed as one decimal reference. Reading only
 * its first digit emits a register name followed by literal `0`, which the
 * assembler rejects before this execution check can run. */
#if defined(__x86_64__)
static int tenth(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7,
                 int a8, int a9)
{
    int out;

    __asm__("movl %10, %0"
            : "=r"(out)
            : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6),
              "r"(a7), "r"(a8), "r"(a9));
    return out;
}

int main(void)
{
    return tenth(0, 1, 2, 3, 4, 5, 6, 7, 8, 19) != 19;
}
#else
int main(void)
{
    return 0;
}
#endif
