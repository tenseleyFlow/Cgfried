// OPT_EQ: all
// Shifts at 0/31/63 (UB widths avoided by masking in source).
// EXIT_CODE: 0
int main(void)
{
    volatile unsigned u = 0x80000000u;
    volatile unsigned long ul = 0x8000000000000000ul;
    volatile int s = -8;
    if ((u >> 31) != 1)
        return 1;
    if ((u << 0) != 0x80000000u)
        return 2;
    if ((ul >> 63) != 1)
        return 3;
    if ((s >> 2) != -2)
        return 4;
    if ((1u << 31) != 0x80000000u)
        return 5;
    return 0;
}
