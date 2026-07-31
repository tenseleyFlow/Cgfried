// The sticky-bit conversion at runtime: 0x8000000000000401 must round
// once, not twice.
// EXIT_CODE: 0
int main(void)
{
    volatile unsigned long v = 0x8000000000000401ul;
    double d = (double)v;
    unsigned long back = (unsigned long)d;
    /* ulp at 2^63 is 2048: 2^63+1025 is 1023 below 2^63+2048 and 1025
     * above 2^63 — it rounds UP (the sticky bit is what makes the
     * distance comparison exact). */
    if (back != 0x8000000000000800ul)
        return 1;
    volatile double big = 9223372036854777856.0; /* 2^63 + 2048 */
    if ((unsigned long)big != 0x8000000000000800ul)
        return 2;
    return 0;
}
