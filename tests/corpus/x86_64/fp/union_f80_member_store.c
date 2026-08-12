// FLAGS: -std=gnu17
// OPT_EQ: all
// EXIT_CODE: 0
/* DSE must treat the target-sized f80 load as a real memory read.  musl uses
 * this exact union shape in frexpl and the printf/scanf conversion core. */
#if defined(__x86_64__)
union ldshape {
    long double f;
    struct {
        unsigned long long m;
        unsigned short se;
    } i;
};

static long double half(long double x)
{
    union ldshape u = {x};

    u.i.se &= 0x8000;
    u.i.se |= 0x3ffe;
    return u.f;
}
#endif

int main(void)
{
#if defined(__x86_64__)
    return half(1.0L) != 0.5L;
#else
    return 0;
#endif
}
