// u64<->f64 branch sequences with the edge set (sticky bit included).
// EXIT_CODE: 0
int main(void)
{
    volatile unsigned long v1 = 0x8000000000000401ul; /* sticky case */
    volatile unsigned long v2 = 0xfffffffffffff800ul; /* exactly
        representable: ulp in [2^63, 2^64) is 2048 — the first draft
        used 2^64-1024, which rounds UP TO 2^64 (round-to-even) */
    volatile unsigned long v3 = 0x7ffffffffffffffful;
    volatile double big = 9223372036854777856.0; /* 2^63 + 2048 */
    if ((unsigned long)(double)v1 != 0x8000000000000800ul)
        return 1;
    if ((unsigned long)(double)v2 != 0xfffffffffffff800ul)
        return 2;
    if ((double)v3 != 9223372036854775808.0)
        return 3;
    if ((unsigned long)big != 0x8000000000000800ul)
        return 4;
    return 0;
}
