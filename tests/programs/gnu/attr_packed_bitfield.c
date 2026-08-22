// EXIT_CODE: 0
// Packed bitfields are byte-addressed by lowering because a field may begin at
// any bit and may span one byte more than its declared integer type. The long
// field below is the boundary case: seven leading bits plus 64 value bits
// occupy nine bytes, so no single scalar container can represent the access.

struct FullPacked {
    unsigned char lead;
    unsigned b : 3;
    unsigned c : 30;
} __attribute__((packed));

struct SuffixPacked {
    unsigned half : 16;
    unsigned long whole : 32 __attribute__((packed));
};

struct Crossing {
    unsigned a : 31;
    unsigned b : 31 __attribute__((packed));
};

struct NineBytes {
    unsigned char a : 7;
    unsigned long b : 64 __attribute__((packed));
};

struct Barrier {
    unsigned a : 3;
    unsigned : 0 __attribute__((packed));
    unsigned b : 3;
} __attribute__((packed));

struct SignedCrossing {
    unsigned lead : 5;
    signed value : 9 __attribute__((packed));
};

struct NestedPacked {
    unsigned char prefix;
    struct {
        unsigned char lead : 7;
        unsigned long value : 64 __attribute__((packed));
    };
};

static struct FullPacked static_full = {1, 7, 0x3fffffffU};
static struct NineBytes static_nine = {0x7f, ~0UL};
static volatile struct Crossing volatile_crossing;

static int bytes_are(const void *ptr, const unsigned char *want,
                     unsigned long n)
{
    const unsigned char *got = ptr;
    unsigned long i;

    for (i = 0; i < n; i++)
        if (got[i] != want[i])
            return 0;
    return 1;
}

static struct NineBytes roundtrip(struct NineBytes value)
{
    value.b ^= 0x55aa55aa55aa55aaUL;
    return value;
}

static struct NineBytes initialized_at_runtime(unsigned long value)
{
    struct NineBytes result = {0x55, value};

    return result;
}

int main(void)
{
    static const unsigned char full_bytes[6] = {1, 0xff, 0xff, 0xff, 0xff, 1};
    static const unsigned char suffix_bytes[8] = {0xff, 0xff, 0xff, 0xff,
                                                  0xff, 0xff, 0,    0};
    static const unsigned char crossing_bytes[8] = {0xff, 0xff, 0xff, 0xff,
                                                    0xff, 0xff, 0xff, 0x3f};
    static const unsigned char nine_bytes[9] = {0xff, 0xff, 0xff, 0xff, 0xff,
                                                0xff, 0xff, 0xff, 0x7f};
    struct FullPacked full = {0};
    struct SuffixPacked suffix = {0};
    struct Crossing crossing = {0};
    struct NineBytes nine = {0};
    struct Barrier barrier = {0};
    struct SignedCrossing signed_crossing = {0};
    struct NestedPacked nested = {0};
    struct NineBytes returned;

    full.lead = 1;
    full.b = 7;
    full.c = 0x3fffffffU;
    if (full.b != 7 || full.c != 0x3fffffffU ||
        !bytes_are(&full, full_bytes, sizeof full))
        return 1;

    suffix.half = 0xffffU;
    suffix.whole = 0xffffffffUL;
    if (suffix.half != 0xffffU || suffix.whole != 0xffffffffUL ||
        !bytes_are(&suffix, suffix_bytes, sizeof suffix))
        return 2;

    crossing.a = 0x7fffffffU;
    crossing.b = 0x7fffffffU;
    if (crossing.a != 0x7fffffffU || crossing.b != 0x7fffffffU ||
        !bytes_are(&crossing, crossing_bytes, sizeof crossing))
        return 3;

    nine.a = 0x7f;
    nine.b = ~0UL;
    if (nine.a != 0x7f || nine.b != ~0UL ||
        !bytes_are(&nine, nine_bytes, sizeof nine))
        return 4;

    barrier.a = 7;
    barrier.b = 7;
    if (barrier.a != 7 || barrier.b != 7 ||
        ((const unsigned char *)&barrier)[0] != 7 ||
        ((const unsigned char *)&barrier)[4] != 7)
        return 5;

    if (!bytes_are(&static_full, full_bytes, sizeof static_full) ||
        !bytes_are(&static_nine, nine_bytes, sizeof static_nine))
        return 6;

    signed_crossing.lead = 0x1f;
    signed_crossing.value = -17;
    if (signed_crossing.lead != 0x1f || signed_crossing.value++ != -17 ||
        signed_crossing.value != -16)
        return 7;

    returned = roundtrip(nine);
    if (returned.a != nine.a || returned.b != (nine.b ^ 0x55aa55aa55aa55aaUL))
        return 8;

    returned = initialized_at_runtime(0xfedcba9876543210UL);
    if (returned.a != 0x55 || returned.b != 0xfedcba9876543210UL)
        return 9;

    nested.prefix = 0xa5;
    nested.lead = 0x55;
    nested.value = 0x0123456789abcdefUL;
    if (nested.prefix != 0xa5 || nested.lead != 0x55 ||
        nested.value != 0x0123456789abcdefUL)
        return 10;

    volatile_crossing.a = 0x7fffffffU;
    volatile_crossing.b = 0x12345678U;
    if (volatile_crossing.a != 0x7fffffffU ||
        volatile_crossing.b != 0x12345678U)
        return 11;
    return 0;
}
