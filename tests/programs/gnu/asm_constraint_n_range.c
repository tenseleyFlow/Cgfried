// FLAGS: -S -std=gnu17
// ERROR_EXPECTED: an asm operand with constraint "N" must be an unsigned 8-bit
// integer constant Unlike Nd, bare N has no register fallback.
int f(void)
{
    int out;

    __asm__("movl %1, %0" : "=r"(out) : "N"(256));
    return out;
}
