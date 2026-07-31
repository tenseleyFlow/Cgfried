// Wraparound is DEFINED for unsigned; the 0x80000000 imm trap rides
// along as a regression.
// EXIT_CODE: 0
int main(void)
{
    volatile unsigned u = 0xffffffffu;
    volatile unsigned long v = 0x8000000000000000ul;
    if (u + 1 != 0)
        return 1;
    if (v + v != 0)
        return 2;
    if ((unsigned)(0u - 1u) != 0xffffffffu)
        return 3;
    return 0;
}
