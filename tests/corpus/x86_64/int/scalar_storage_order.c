/* GNU scalar_storage_order changes the physical image of scalar record
 * members without changing their logical C values. All current Cgfried
 * targets are little-endian, so a big-endian record exercises the reverse
 * path and a little-endian record is the confirming control. */

#define BE __attribute__((scalar_storage_order("big-endian")))
#define LE __attribute__((scalar_storage_order("little-endian")))

struct Bits16 {
    unsigned short i : 12;
    unsigned char c1 : 1;
    unsigned char c2 : 1;
    unsigned char c3 : 1;
    unsigned char c4 : 1;
} BE;

struct Bits32 {
    unsigned int i : 24;
    unsigned char c1 : 1;
    unsigned char c2 : 1;
    unsigned char c3 : 1;
    unsigned char c4 : 1;
    unsigned char c5 : 1;
    unsigned char c6 : 1;
    unsigned char c7 : 1;
    unsigned char c8 : 1;
} BE;

struct RuntimeBits {
    unsigned int a : 5;
    signed int b : 7;
    unsigned int c : 20;
} BE;

struct Integral {
    unsigned short h;
    unsigned int w;
    unsigned short a[2];
    unsigned char c;
} BE;

struct ConfirmingBits {
    unsigned short i : 12;
    unsigned char c1 : 1;
    unsigned char c2 : 1;
    unsigned char c3 : 1;
    unsigned char c4 : 1;
} LE;

struct PackedBits {
    unsigned char a : 3;
    unsigned short b : 10;
    unsigned char c : 3;
} __attribute__((packed)) BE;

union IntegralUnion {
    unsigned short h;
    unsigned int w;
    unsigned char c;
} BE;

struct PointerMember {
    int *p;
    unsigned int w;
} BE;

struct Inner {
    unsigned int value;
};

struct NestedAggregate {
    struct Inner inner;
    unsigned int value;
} BE;

struct LongIntegral {
    unsigned long long value;
} BE;

static struct Bits16 g16 = {0x155, 1, 1, 1, 1};
static struct Bits32 g32 = {0x123456, 1, 1, 1, 1, 1, 1, 1, 1};
static struct Integral gi = {0x1234, 0x89abcdef, {0x1357, 0x2468}, 0xaa};
static struct ConfirmingBits gle = {0x155, 1, 1, 1, 1};
static struct PackedBits gpacked = {5, 0x2aa, 3};
static union IntegralUnion gunion = {.w = 0x12345678};
static struct NestedAggregate gnested = {{0x12345678}, 0x12345678};
static struct LongIntegral glong = {0x0123456789abcdefULL};
static int anchor;

static int check_static_images(void)
{
    unsigned char *p16 = (unsigned char *)&g16;
    unsigned char *p32 = (unsigned char *)&g32;
    unsigned char *pi = (unsigned char *)&gi;
    unsigned char *ple = (unsigned char *)&gle;
    unsigned char *ppacked = (unsigned char *)&gpacked;
    unsigned char *punion = (unsigned char *)&gunion;
    unsigned char *pnested = (unsigned char *)&gnested;
    unsigned char *plong = (unsigned char *)&glong;

    if (p16[0] != 0x15 || p16[1] != 0x5f)
        return 1;
    if (p32[0] != 0x12 || p32[1] != 0x34 || p32[2] != 0x56 || p32[3] != 0xff)
        return 2;
    if (pi[0] != 0x12 || pi[1] != 0x34 || pi[4] != 0x89 || pi[5] != 0xab ||
        pi[6] != 0xcd || pi[7] != 0xef || pi[8] != 0x13 || pi[9] != 0x57 ||
        pi[10] != 0x24 || pi[11] != 0x68 || pi[12] != 0xaa)
        return 3;
    if (ple[0] != 0x55 || ple[1] != 0xf1)
        return 4;
    if (sizeof(gpacked) != 2 || ppacked[0] != 0xb5 || ppacked[1] != 0x53 ||
        gpacked.a != 5 || gpacked.b != 0x2aa || gpacked.c != 3)
        return 5;
    if (punion[0] != 0x12 || punion[1] != 0x34 || punion[2] != 0x56 ||
        punion[3] != 0x78 || gunion.w != 0x12345678)
        return 6;
    if (pnested[0] != 0x78 || pnested[1] != 0x56 || pnested[2] != 0x34 ||
        pnested[3] != 0x12 || pnested[4] != 0x12 || pnested[5] != 0x34 ||
        pnested[6] != 0x56 || pnested[7] != 0x78 ||
        gnested.inner.value != 0x12345678 || gnested.value != 0x12345678)
        return 7;
    if (plong[0] != 0x01 || plong[1] != 0x23 || plong[2] != 0x45 ||
        plong[3] != 0x67 || plong[4] != 0x89 || plong[5] != 0xab ||
        plong[6] != 0xcd || plong[7] != 0xef ||
        glong.value != 0x0123456789abcdefULL)
        return 8;
    if (g16.i != 0x155 || g32.i != 0x123456 || gi.h != 0x1234 ||
        gi.w != 0x89abcdef || gi.a[0] != 0x1357 || gi.a[1] != 0x2468 ||
        gi.c != 0xaa || gle.i != 0x155)
        return 9;
    return 0;
}

static int check_runtime_bits(unsigned int a, int b, unsigned int c)
{
    struct RuntimeBits initialized = {a, b, c};
    struct RuntimeBits r = {0, 0, 0};
    unsigned char *p = (unsigned char *)&r;
    unsigned char *pi = (unsigned char *)&initialized;

    if (initialized.a != 17 || initialized.b != -5 ||
        initialized.c != 0xabcde || pi[0] != 0x8f || pi[1] != 0xba ||
        pi[2] != 0xbc || pi[3] != 0xde)
        return 10;

    r.a = a;
    r.b = b;
    r.c = c;
    if (r.a != 17 || r.b != -5 || r.c != 0xabcde)
        return 11;
    if (p[0] != 0x8f || p[1] != 0xba || p[2] != 0xbc || p[3] != 0xde)
        return 12;

    r.a += 3;
    r.b++;
    r.c ^= 0x11111;
    if (r.a != 20 || r.b != -4 || r.c != 0xbadcf)
        return 13;
    if (p[0] != 0xa7 || p[1] != 0xcb || p[2] != 0xad || p[3] != 0xcf)
        return 14;
    return 0;
}

static int check_runtime_integrals(unsigned short h, unsigned int w,
                                   unsigned short a0, unsigned short a1)
{
    struct Integral initialized = {h, w, {a0, a1}, 0x5a};
    struct Integral x = {0, 0, {0, 0}, 0};
    unsigned char *p = (unsigned char *)&x;
    unsigned char *pi = (unsigned char *)&initialized;

    if (initialized.h != 0x1234 || initialized.w != 0x89abcdef ||
        initialized.a[0] != 0x1357 || initialized.a[1] != 0x2468 ||
        initialized.c != 0x5a || pi[0] != 0x12 || pi[1] != 0x34 ||
        pi[4] != 0x89 || pi[5] != 0xab || pi[6] != 0xcd || pi[7] != 0xef ||
        pi[8] != 0x13 || pi[9] != 0x57 || pi[10] != 0x24 || pi[11] != 0x68 ||
        pi[12] != 0x5a)
        return 15;

    x.h = h;
    x.w = w;
    x.a[0] = a0;
    x.a[1] = a1;
    x.c = 0x5a;
    if (x.h != 0x1234 || x.w != 0x89abcdef || x.a[0] != 0x1357 ||
        x.a[1] != 0x2468 || x.c != 0x5a)
        return 16;
    if (p[0] != 0x12 || p[1] != 0x34 || p[4] != 0x89 || p[5] != 0xab ||
        p[6] != 0xcd || p[7] != 0xef || p[8] != 0x13 || p[9] != 0x57 ||
        p[10] != 0x24 || p[11] != 0x68 || p[12] != 0x5a)
        return 17;
    return 0;
}

static int check_runtime_aggregate_edges(int *p, unsigned int value)
{
    struct PointerMember pointer = {p, value};
    union IntegralUnion u = {.w = value};
    struct PackedBits bits = {5, 0x2aa, 3};
    struct NestedAggregate nested = {{value}, value};
    struct LongIntegral wide = {0x0123456789abcdefULL};
    unsigned char *pu = (unsigned char *)&u;
    unsigned char *pb = (unsigned char *)&bits;
    unsigned char *pn = (unsigned char *)&nested;
    unsigned char *pw = (unsigned char *)&wide;

    if (pointer.p != p || pointer.w != 0x12345678)
        return 18;
    if (u.w != 0x12345678 || pu[0] != 0x12 || pu[1] != 0x34 || pu[2] != 0x56 ||
        pu[3] != 0x78)
        return 19;
    if (sizeof(bits) != 2 || bits.a != 5 || bits.b != 0x2aa || bits.c != 3 ||
        pb[0] != 0xb5 || pb[1] != 0x53)
        return 20;
    if (nested.inner.value != 0x12345678 || nested.value != 0x12345678 ||
        pn[0] != 0x78 || pn[1] != 0x56 || pn[2] != 0x34 || pn[3] != 0x12 ||
        pn[4] != 0x12 || pn[5] != 0x34 || pn[6] != 0x56 || pn[7] != 0x78)
        return 21;
    if (wide.value != 0x0123456789abcdefULL || pw[0] != 0x01 || pw[1] != 0x23 ||
        pw[2] != 0x45 || pw[3] != 0x67 || pw[4] != 0x89 || pw[5] != 0xab ||
        pw[6] != 0xcd || pw[7] != 0xef)
        return 22;
    return 0;
}

int main(void)
{
    int result = check_static_images();

    if (result)
        return result;
    result = check_runtime_bits(17, -5, 0xabcde);
    if (result)
        return result;
    result = check_runtime_integrals(0x1234, 0x89abcdef, 0x1357, 0x2468);
    if (result)
        return result;
    return check_runtime_aggregate_edges(&anchor, 0x12345678);
}
