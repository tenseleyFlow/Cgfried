// OPT_EQ: all
// The Sprint 23 conversion matrix at runtime: trunc-toward-zero,
// widths, and the free-zext u32 path.
// EXIT_CODE: 0
int main(void)
{
    volatile double d = 2.9;
    volatile double dn = -2.9;
    volatile float f = 16777217.0f; /* 2^24+1 is not a float */
    volatile unsigned u = 0xffffffffu;
    if ((int)d != 2)
        return 1;
    if ((int)dn != -2)
        return 2;
    if ((long)d != 2)
        return 3;
    if (f == 16777217.0f) { /* rounded to 2^24 at the literal */
        volatile double dd = f;
        if (dd != 16777216.0)
            return 4;
    }
    if ((double)u != 4294967295.0)
        return 5;
    /* (float)1000000007 rounds — compared AS DOUBLE the loss shows
     * (comparing float-vs-float-literal hides it: both round the same
     * way — the first draft of this fixture made exactly that error) */
    if ((double)(float)1000000007 == 1000000007.0)
        return 6;
    return 0;
}
