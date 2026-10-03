// EXIT_CODE: 0
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
// GCC accepts indexed atomic-array accesses in a reverse-order record even
// though it rejects the corresponding direct scalar-member spelling through
// its internal address-taking check. Keep this fixture on the shared surface.
#define BE __attribute__((scalar_storage_order("big-endian")))

struct Reverse {
    _Atomic(unsigned short) marker;
    _Atomic(unsigned int) values[2];
    _Atomic(unsigned long long) wide[2];
    volatile _Atomic(signed int) observed[2];
} BE;

static struct Reverse object = {
    0x1234,
    {7, 0x10203040},
    {0x0102030405060708ULL, 0x1112131415161718ULL},
    {-2, 3},
};

static int bytes_equal(const unsigned char *actual,
                       const unsigned char *expected, unsigned count)
{
    unsigned i;

    for (i = 0; i < count; i++)
        if (actual[i] != expected[i])
            return 0;
    return 1;
}

int main(void)
{
    static const unsigned char initial[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x10, 0x20,
        0x30, 0x40, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
        0x17, 0x18, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x03,
    };
    static const unsigned char final[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x10, 0x20,
        0x30, 0x41, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
        0x17, 0x19, 0xff, 0xff, 0xff, 0xfb, 0x00, 0x00, 0x00, 0x03,
    };
    unsigned int old;
    unsigned int assigned;

    if (!bytes_equal((const unsigned char *)&object, initial,
                     (unsigned)sizeof(initial)))
        return 1;
    if (object.values[0] != 7 || object.values[1] != 0x10203040 ||
        object.wide[0] != 0x0102030405060708ULL || object.observed[0] != -2)
        return 2;

    old = object.values[1]++;
    if (old != 0x10203040 || object.values[1] != 0x10203041)
        return 3;
    assigned = (object.values[0] = 5);
    if (assigned != 5)
        return 4;
    object.values[0] *= 3;
    object.values[0] /= 3;
    object.values[0] %= 4;
    object.values[0] <<= 8;
    object.values[0] >>= 4;
    object.values[0] |= 0x20;
    object.values[0] ^= 0x11;
    object.values[0] &= 0x0f;
    object.values[0] += 4;
    object.values[0] -= 2;
    if (object.values[0] != 3)
        return 5;
    if (++object.wide[1] != 0x1112131415161719ULL)
        return 6;
    object.observed[0] -= 3;
    if (object.observed[0] != -5)
        return 7;
    if (!bytes_equal((const unsigned char *)&object, final,
                     (unsigned)sizeof(final)))
        return 8;
    return 0;
}
