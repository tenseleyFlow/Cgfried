// OPT_EQ: all
// A frame past every one of the AArch64 immediate boundaries, which the
// other 52 corpus programs are all comfortably inside.
//
// The three limits, all different, all silent until an assembler sees them:
//   stp/ldp pre/post-index  signed 7-bit scaled by 8  -> [-512, +504]
//   stp/ldp scaled offset   same field                -> [-512, +504]
//   add/sub immediate       12 bits, optionally <<12  -> 4095, or a
//                                                        multiple of 4096
// 8000 bytes of locals clears all three, so both the SP adjustment and the
// frame-object addressing need their wide forms. `volatile` keeps the array
// alive; the sums are the check that the addressing actually landed where
// the stores did rather than merely assembling.
// EXIT_CODE: 0
int main(void)
{
    volatile int a[2000];
    volatile long tail = 0;
    int i;
    long s = 0;

    for (i = 0; i < 2000; i++)
        a[i] = i;
    tail = a[1999];
    for (i = 0; i < 2000; i++)
        s += a[i];
    if (s != 1999000L)
        return 1;
    if (tail != 1999L)
        return 2;
    /* The first and last elements bracket the object, so an addressing form
     * that silently truncated its offset shows up here rather than in the
     * sum, which a wrong base could still get right by accident. */
    if (a[0] != 0 || a[1999] != 1999)
        return 3;
    return 0;
}
