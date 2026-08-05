// OPT_EQ: all
// The two long-double behaviours a soft-float implementation gets wrong
// silently. Both are target-independent: x86_64 answers them with x87 f80,
// arm64-linux with binary128 libcalls, and every value here is exact in
// both formats.
//
// 1. Negation is a SIGN-BIT FLIP, not `0 - x`. The two agree on every
//    value except zero, where `0 - (+0.0)` is +0.0 and `-(+0.0)` is -0.0.
//    Dividing into the result is how you see the difference without a
//    signbit() that might itself be miscompiled.
// 2. A NaN is UNORDERED, so `<` and `==` are false AND `!=` is true --
//    which is one call and two different tests when the comparison is a
//    libcall returning a signed int (__lttf2 and friends).
// EXIT_CODE: 0
int main(void)
{
    volatile long double pz = 0.0L, nz = -0.0L;
    volatile long double one = 1.0L, two = 2.0L;
    volatile double dz;
    volatile long double nan;
    volatile long double huge = 1.0e4000L;

    dz = (double)(-pz);
    if (1.0 / dz > 0.0)
        return 1; /* -(+0.0) must be NEGATIVE zero */
    dz = (double)(-nz);
    if (1.0 / dz < 0.0)
        return 2; /* -(-0.0) must be POSITIVE zero */
    if ((double)(-(-one)) != 1.0)
        return 3;

    if (!(one < two) || (two < one))
        return 4;
    if (!(two > one) || !(one <= two) || !(two >= one))
        return 5;
    if ((one == two) || !(one != two))
        return 6;

    /* Built rather than spelled: a NaN literal needs <math.h>, and the
     * point is to reach the unordered path without depending on one.
     * 1e4000 squared overflows to infinity in BOTH f80 and binary128 --
     * their exponent ranges are the same -- and inf - inf is NaN. */
    nan = huge * huge - huge * huge;
    if (nan == nan)
        return 7; /* unordered: false */
    if (!(nan != nan))
        return 8; /* unordered: TRUE */
    if (nan < one || nan > one || nan <= one || nan >= one)
        return 9;
    return 0;
}
