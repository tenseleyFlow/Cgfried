// OPT_EQ: all
// A frame of EXACTLY 512 bytes, which is one 16-byte step wide and is the
// only size that catches this bug.
//
// stp/ldp pre- and post-index share one field: a signed 7-bit value scaled
// by 8, so the range is [-512, +504] and is ASYMMETRIC. A 512-byte frame
// therefore assembles its prologue -- `stp x29, x30, [sp, #-512]!` is legal
// -- and then fails on the matching `ldp x29, x30, [sp], #512`. One byte
// less takes the 496 path and one 16-byte step more takes the separate
// sub-sp path, so a fixture that merely has a "big" frame sails past it:
// int/big_frame.c, at 8000 bytes, never touches this form at all.
//
// 456 bytes of locals plus the saved pair and callee-saved registers is what
// lands on 512 for this backend today; with the bound wrongly at 512 this
// exact program emitted the illegal ldp, and with it at 504 the same program
// takes the separate sub-sp path instead. Either way the fixture fails if the
// bound regresses. If the frame layout drifts the size moves and this stops
// testing what it names -- there is no way to assert a frame size from C, so
// that risk is stated rather than checked.
// CHECK: 26940
// EXIT_CODE: 0
int printf(const char *, ...);
int main(void)
{
    volatile char a[456];
    int i;
    long s = 0;

    for (i = 0; i < 456; i++)
        a[i] = (char)(i & 0x7f);
    for (i = 0; i < 456; i++)
        s += a[i];
    if (a[0] != 0 || a[455] != (char)(455 & 0x7f))
        return 1;
    printf("%ld\n", s);
    return 0;
}
