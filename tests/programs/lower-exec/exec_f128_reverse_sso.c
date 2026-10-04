// EXIT_CODE: 0
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
#define BE __attribute__((scalar_storage_order("big-endian")))

struct F128Box {
    _Float128 value;
    _Float128 values[2];
} BE;

struct AtomicF128Box {
    _Atomic(_Float128) values[2];
} BE;

struct F128View {
    _Float128 value;
};

typedef struct F128View F128ViewBE BE;

union F128Bits {
    _Float128 value;
    unsigned long long words[2];
};

static struct F128Box sf = {1.0F128, {2.0F128, -3.0F128}};
static struct AtomicF128Box sa = {{4.0F128, 5.0F128}};
static F128ViewBE sv = {8.0F128};

static int check_initial(void)
{
    const unsigned char *pf = (const unsigned char *)&sf;
    const unsigned char *pa = (const unsigned char *)&sa;
    const unsigned char *pv = (const unsigned char *)&sv;

    if (sf.value != 1.0F128 || sf.values[0] != 2.0F128 ||
        sf.values[1] != -3.0F128)
        return 1;
    if (pf[0] != 0x3f || pf[1] != 0xff || pf[16] != 0x40 || pf[17] != 0x00 ||
        pf[32] != 0xc0 || pf[33] != 0x00 || pf[34] != 0x80)
        return 2;
    if (sa.values[0] != 4.0F128 || sa.values[1] != 5.0F128)
        return 3;
    if (pa[0] != 0x40 || pa[1] != 0x01 || pa[16] != 0x40 || pa[17] != 0x01 ||
        pa[18] != 0x40)
        return 4;
    if (sv.value != 8.0F128 || pv[0] != 0x40 || pv[1] != 0x02)
        return 17;
    return 0;
}

static int check_updates(void)
{
    const unsigned char *pf = (const unsigned char *)&sf;
    const unsigned char *pa = (const unsigned char *)&sa;
    const unsigned char *pv = (const unsigned char *)&sv;
    union F128Bits input;
    union F128Bits output;
    _Float128 assigned;
    _Float128 old;

    sf.value += 2.0F128;
    assigned = (sf.values[0] = -1.5F128);
    if (sf.value != 3.0F128 || assigned != -1.5F128 || sf.values[0] != -1.5F128)
        return 5;
    if (pf[0] != 0x40 || pf[1] != 0x00 || pf[2] != 0x80 || pf[16] != 0xbf ||
        pf[17] != 0xff || pf[18] != 0x80)
        return 6;

    input.words[0] = 0x12345ull;
    input.words[1] = 0x7fff800000000000ull;
    sf.value = input.value;
    output.value = sf.value;
    if (output.words[0] != input.words[0] || output.words[1] != input.words[1])
        return 7;
    if (pf[0] != 0x7f || pf[1] != 0xff || pf[2] != 0x80 || pf[13] != 0x01 ||
        pf[14] != 0x23 || pf[15] != 0x45)
        return 8;

    old = sa.values[0]++;
    sa.values[1] += 2.0F128;
    if (old != 4.0F128 || sa.values[0] != 5.0F128 || sa.values[1] != 7.0F128)
        return 9;
    if (pa[0] != 0x40 || pa[1] != 0x01 || pa[2] != 0x40 || pa[16] != 0x40 ||
        pa[17] != 0x01 || pa[18] != 0xc0)
        return 10;

    sa.values[0] = input.value;
    output.value = sa.values[0];
    if (output.words[0] != input.words[0] || output.words[1] != input.words[1])
        return 11;
    if (pa[0] != 0x7f || pa[1] != 0xff || pa[2] != 0x80 || pa[13] != 0x01 ||
        pa[14] != 0x23 || pa[15] != 0x45)
        return 12;
    sv.value += 1.0F128;
    if (sv.value != 9.0F128 || pv[0] != 0x40 || pv[1] != 0x02 || pv[2] != 0x20)
        return 18;
    return 0;
}

static int check_runtime_init(_Float128 value)
{
    struct F128Box local = {value, {value + 1.0F128, -value}};
    struct AtomicF128Box atomic = {{value, value + 1.0F128}};
    F128ViewBE view = {value};
    const unsigned char *pl = (const unsigned char *)&local;
    const unsigned char *pa = (const unsigned char *)&atomic;
    const unsigned char *pv = (const unsigned char *)&view;

    if (local.value != 6.0F128 || local.values[0] != 7.0F128 ||
        local.values[1] != -6.0F128)
        return 13;
    if (pl[0] != 0x40 || pl[1] != 0x01 || pl[2] != 0x80 || pl[16] != 0x40 ||
        pl[17] != 0x01 || pl[18] != 0xc0 || pl[32] != 0xc0 || pl[33] != 0x01 ||
        pl[34] != 0x80)
        return 14;
    if (atomic.values[0] != 6.0F128 || atomic.values[1] != 7.0F128)
        return 15;
    if (pa[0] != 0x40 || pa[1] != 0x01 || pa[2] != 0x80 || pa[16] != 0x40 ||
        pa[17] != 0x01 || pa[18] != 0xc0)
        return 16;
    if (view.value != 6.0F128 || pv[0] != 0x40 || pv[1] != 0x01 ||
        pv[2] != 0x80)
        return 19;
    return 0;
}

int main(void)
{
    int result = check_initial();

    if (!result)
        result = check_updates();
    return result ? result : check_runtime_init(6.0F128);
}
