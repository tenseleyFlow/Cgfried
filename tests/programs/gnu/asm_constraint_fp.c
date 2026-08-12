// FLAGS: -O2 -std=gnu17
// EXIT_CODE: 0
static double abs_double(double x)
{
    double mask;

    __asm__("pcmpeqd %0, %0" : "=x"(mask));
    __asm__("psrlq $1, %0" : "+x"(mask));
    __asm__("andps %1, %0" : "+x"(x) : "x"(mask));
    return x;
}

static float abs_float(float x)
{
    float mask;

    __asm__("pcmpeqd %0, %0" : "=x"(mask));
    __asm__("psrld $1, %0" : "+x"(mask));
    __asm__("andps %1, %0" : "+x"(x) : "x"(mask));
    return x;
}

static long double abs_long_double(long double x)
{
    __asm__("fabs" : "+t"(x));
    return x;
}

static long remainder_status(long double x, long double y)
{
    unsigned short status;

    do
        __asm__("fprem; fnstsw %%ax" : "+t"(x), "=a"(status) : "u"(y));
    while (status & 0x400);
    return (long)x;
}

static long convert_long_double(long double x)
{
    long out;

    __asm__("fistpll %0" : "=m"(out) : "t"(x) : "st");
    return out;
}

int main(void)
{
    if (abs_double(-3.5) != 3.5)
        return 1;
    if (abs_float(-2.25f) != 2.25f)
        return 2;
    if (abs_long_double(-7.0L) != 7.0L)
        return 3;
    if (remainder_status(17.0L, 5.0L) != 2)
        return 4;
    if (convert_long_double(9.0L) != 9)
        return 5;
    return 0;
}
