// SKIP(*): staged for execution at Sprint 25 (no backend yet)
// The sticky-bit conversion at runtime: 0x8000000000000401 must round
// once, not twice.
// EXIT_CODE: 0
int main(void) {
    volatile unsigned long v = 0x8000000000000401ul;
    double d = (double)v;
    unsigned long back = (unsigned long)d;
    if (back != 0x8000000000000400ul) return 1;
    volatile double big = 9223372036854777856.0; /* 2^63 + 2048 */
    if ((unsigned long)big != 0x8000000000000800ul) return 2;
    return 0;
}
