// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: which is also named in the clobber list
/* GCC rejects an operand and clobber that name the same register. */
int f(void)
{
    int out;

    __asm__("" : "=a"(out) : : "rax");
    return out;
}
