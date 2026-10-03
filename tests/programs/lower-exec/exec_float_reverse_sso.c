// EXIT_CODE: 0
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
// GCC accepts ordinary floating members and indexed atomic-array accesses in
// a reverse-order record. Keep this fixture on that shared source surface.
#define BE __attribute__((scalar_storage_order("big-endian")))

struct F32Box {
    float value;
    float values[2];
} BE;

struct F64Box {
    double value;
    double values[2];
} BE;

struct ExtBox {
    _Float32 f32;
    _Float64 f64;
    _Float32x f32x;
} BE;

#if __SIZEOF_LONG_DOUBLE__ == 8
struct LongDoubleBox {
    long double value;
} BE;
#endif

struct AtomicBox {
    _Atomic(float) f[2];
    _Atomic(double) d[2];
} BE;

union F32Bits {
    float value;
    unsigned bits;
};

static struct F32Box sf = {1.0f, {2.0f, -3.0f}};
static struct F64Box sd = {1.0, {2.0, -3.0}};
static struct ExtBox se = {1.0F32, 2.0F64, 3.0F32x};
#if __SIZEOF_LONG_DOUBLE__ == 8
static struct LongDoubleBox sld = {4.0L};
#endif
static struct AtomicBox sa = {{5.0f, 6.0f}, {7.0, 8.0}};

static int check_initial(void)
{
    const unsigned char *pf = (const unsigned char *)&sf;
    const unsigned char *pd = (const unsigned char *)&sd;
    const unsigned char *pe = (const unsigned char *)&se;
    const unsigned char *pa = (const unsigned char *)&sa;

    if (sf.value != 1.0f || sf.values[0] != 2.0f || sf.values[1] != -3.0f)
        return 1;
    if (pf[0] != 0x3f || pf[1] != 0x80 || pf[4] != 0x40 || pf[5] != 0x00 ||
        pf[8] != 0xc0 || pf[9] != 0x40)
        return 2;
    if (sd.value != 1.0 || sd.values[0] != 2.0 || sd.values[1] != -3.0)
        return 3;
    if (pd[0] != 0x3f || pd[1] != 0xf0 || pd[8] != 0x40 || pd[9] != 0x00 ||
        pd[16] != 0xc0 || pd[17] != 0x08)
        return 4;
    if (se.f32 != 1.0F32 || se.f64 != 2.0F64 || se.f32x != 3.0F32x)
        return 5;
    if (pe[0] != 0x3f || pe[1] != 0x80 || pe[8] != 0x40 || pe[9] != 0x00 ||
        pe[16] != 0x40 || pe[17] != 0x08)
        return 6;
#if __SIZEOF_LONG_DOUBLE__ == 8
    {
        const unsigned char *pld = (const unsigned char *)&sld;

        if (sld.value != 4.0L || pld[0] != 0x40 || pld[1] != 0x10)
            return 7;
    }
#endif
    if (sa.f[0] != 5.0f || sa.d[1] != 8.0)
        return 8;
    if (pa[0] != 0x40 || pa[1] != 0xa0 || pa[16] != 0x40 || pa[17] != 0x20)
        return 9;
    return 0;
}

static int check_updates(void)
{
    const unsigned char *pa = (const unsigned char *)&sa;
    union F32Bits input;
    union F32Bits output;
    float old;
    double assigned;

    sf.value += 2.0f;
    sd.values[1] *= -2.0;
    if (sf.value != 3.0f || sd.values[1] != 6.0)
        return 10;

    input.bits = 0x7fc12345U;
    sf.value = input.value;
    output.value = sf.value;
    if (output.bits != input.bits)
        return 11;
    {
        const unsigned char *pf = (const unsigned char *)&sf;

        if (pf[0] != 0x7f || pf[1] != 0xc1 || pf[2] != 0x23 ||
            pf[3] != 0x45)
            return 12;
    }

    old = sa.f[0]++;
    if (old != 5.0f || sa.f[0] != 6.0f)
        return 13;
    sa.f[0] += 1.0f;
    assigned = (sa.d[0] = -1.5);
    if (sa.f[0] != 7.0f || assigned != -1.5 || sa.d[0] != -1.5)
        return 14;
    if (++sa.d[1] != 9.0)
        return 15;
    if (pa[0] != 0x40 || pa[1] != 0xe0 || pa[8] != 0xbf ||
        pa[9] != 0xf8 || pa[16] != 0x40 || pa[17] != 0x22)
        return 16;
    return 0;
}

static int check_runtime_init(float f, double d)
{
    struct F32Box af = {f, {f + 1.0f, f + 2.0f}};
    struct F64Box ad = {d, {d + 1.0, d + 2.0}};
    struct AtomicBox aa = {{f, f + 1.0f}, {d, d + 1.0}};
    const unsigned char *pf = (const unsigned char *)&af;
    const unsigned char *pd = (const unsigned char *)&ad;
    const unsigned char *pa = (const unsigned char *)&aa;

    if (af.value != 1.0f || af.values[0] != 2.0f || af.values[1] != 3.0f)
        return 17;
    if (pf[0] != 0x3f || pf[1] != 0x80 || pf[4] != 0x40 || pf[5] != 0x00 ||
        pf[8] != 0x40 || pf[9] != 0x40)
        return 18;
    if (ad.value != 2.0 || ad.values[0] != 3.0 || ad.values[1] != 4.0)
        return 19;
    if (pd[0] != 0x40 || pd[1] != 0x00 || pd[8] != 0x40 || pd[9] != 0x08 ||
        pd[16] != 0x40 || pd[17] != 0x10)
        return 20;
    if (aa.f[0] != 1.0f || aa.f[1] != 2.0f || aa.d[0] != 2.0 ||
        aa.d[1] != 3.0)
        return 21;
    if (pa[0] != 0x3f || pa[1] != 0x80 || pa[4] != 0x40 || pa[5] != 0x00 ||
        pa[8] != 0x40 || pa[9] != 0x00 || pa[16] != 0x40 || pa[17] != 0x08)
        return 22;
    return 0;
}

int main(void)
{
    int result = check_initial();

    if (!result)
        result = check_updates();
    return result ? result : check_runtime_init(1.0f, 2.0);
}
