typedef int i128 __attribute__((mode(TI)));
typedef unsigned int u128 __attribute__((mode(TI)));

static i128 static_from_float = (i128)42.75;
static double static_from_ti = (double)((i128)1 << 100);
static volatile float volatile_float = -3.75f;
static volatile i128 volatile_ti;
static int calls;

static double once(void)
{
    calls++;
    return 19.875;
}

static int fail(int line)
{
    return line;
}

int main(void)
{
    i128 s;
    u128 u;
    double d;
    float f;
    long double ld;
#ifdef __CGFRIED__
    __float128 q;
#endif

    s = volatile_float;
    if (s != -3)
        return fail(__LINE__);
    s = 0x1p100;
    if (s != ((i128)1 << 100))
        return fail(__LINE__);
    u = 0x1p127;
    if (u != ((u128)1 << 127))
        return fail(__LINE__);
    s = -0x1p127;
    if (s != (i128)((u128)1 << 127))
        return fail(__LINE__);
    s = -0.75f;
    u = -0.75f;
    if (s != 0 || u != 0)
        return fail(__LINE__);

    s = ((i128)1 << 100) + 17;
    f = s;
    d = s;
    if (f != 0x1p100f || d != 0x1p100)
        return fail(__LINE__);
    u = (u128)1 << 127;
    if ((double)u != 0x1p127)
        return fail(__LINE__);
    if ((double)(-((i128)1 << 100)) != -0x1p100)
        return fail(__LINE__);
    ld = s;
    if (ld != 0x1p100L)
        return fail(__LINE__);
    s = (long double)-9.75L;
    if (s != -9)
        return fail(__LINE__);
#ifdef __CGFRIED__
    u = ((u128)1 << 100) + 17;
    q = u;
    if ((u128)q != u)
        return fail(__LINE__);
#endif

    d = ((i128)5) + 0.5;
    if (d != 5.5)
        return fail(__LINE__);
    d = 1 ? (i128)7 : 0.5;
    if (d != 7.0)
        return fail(__LINE__);
    s = 5;
    s += 2.75;
    if (s != 7)
        return fail(__LINE__);

    s = once();
    if (calls != 1 || s != 19)
        return fail(__LINE__);
    volatile_ti = 0x1p80;
    if ((double)volatile_ti != 0x1p80)
        return fail(__LINE__);
    if (static_from_float != 42 || static_from_ti != 0x1p100)
        return fail(__LINE__);
    return 0;
}
