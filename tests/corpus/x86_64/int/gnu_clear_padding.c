// OPT_EQ: all
// Exercise the value-preserving representation canonicalization, not merely
// recognition of the builtin. Every object begins with nonzero padding.

struct S {
    char c;
    int x;
};

struct Bits {
    unsigned a : 3;
    unsigned : 3;
    unsigned b : 2;
};

struct ReverseBits {
    unsigned a : 3;
    unsigned : 3;
    unsigned b : 2;
} __attribute__((scalar_storage_order("big-endian")));

union Full {
    struct S padded;
    unsigned char bytes[sizeof(struct S)];
};

union Common {
    struct S left;
    struct S right;
};

static volatile int requested = 3;
static volatile int pointer_calls;

static void fill(void *object, unsigned long size, unsigned char value)
{
    unsigned char *bytes = object;
    unsigned long i;

    for (i = 0; i < size; i++)
        bytes[i] = value;
}

static struct S *once(struct S *object)
{
    pointer_calls++;
    return object;
}

static int s_padding_is_zero(const struct S *object)
{
    const unsigned char *bytes = (const unsigned char *)object;

    return bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
}

int main(void)
{
    struct S scalar;
    struct Bits bits;
#ifdef __CGFRIED__
    struct ReverseBits reverse_bits;
#endif
    union Full full;
    union Common common;
    struct S fixed[3];
    int count = requested;
    struct S dynamic[count];
    long double extended = 1.0L;
    unsigned char *bytes;
    int i;

    fill(&scalar, sizeof(scalar), 0xff);
    scalar.c = 7;
    scalar.x = 0x12345678;
    __builtin_clear_padding(once(&scalar));
    if (pointer_calls != 1 || scalar.c != 7 || scalar.x != 0x12345678 ||
        !s_padding_is_zero(&scalar))
        return 1;

    fill(&bits, sizeof(bits), 0xff);
    bits.a = 5;
    bits.b = 2;
    __builtin_clear_padding(&bits);
    bytes = (unsigned char *)&bits;
    if (bits.a != 5 || bits.b != 2 || (bytes[0] & 0x38) != 0)
        return 2;
    for (i = 1; i < (int)sizeof(bits); i++)
        if (bytes[i] != 0)
            return 3;

#ifdef __CGFRIED__
    /* GCC 13.3 applies the native-order 0xc7 mask here and changes a from 5
     * to 4, contrary to clear_padding's value-preservation contract. Pin the
     * correct 0xe3 reverse-order mask without making that GCC bug invalidate
     * the rest of this fixture's differential oracle. */
    fill(&reverse_bits, sizeof(reverse_bits), 0xff);
    reverse_bits.a = 5;
    reverse_bits.b = 2;
    __builtin_clear_padding(&reverse_bits);
    bytes = (unsigned char *)&reverse_bits;
    if (reverse_bits.a != 5 || reverse_bits.b != 2 || bytes[0] != 0xa2)
        return 11;
    for (i = 1; i < (int)sizeof(reverse_bits); i++)
        if (bytes[i] != 0)
            return 12;
#endif

    /* A byte-array member makes every union bit a value bit, so nothing is
     * padding in ALL members and the representation must remain untouched. */
    fill(&full, sizeof(full), 0xff);
    __builtin_clear_padding(&full);
    for (i = 0; i < (int)sizeof(full); i++)
        if (full.bytes[i] != 0xff)
            return 4;

    fill(&common, sizeof(common), 0xff);
    common.left.c = 9;
    common.left.x = 77;
    __builtin_clear_padding(&common);
    if (common.left.c != 9 || common.left.x != 77 ||
        !s_padding_is_zero(&common.left))
        return 5;

    fill(&fixed, sizeof(fixed), 0xff);
    for (i = 0; i < 3; i++) {
        fixed[i].c = (char)(i + 1);
        fixed[i].x = i + 20;
    }
    __builtin_clear_padding(&fixed);
    for (i = 0; i < 3; i++)
        if (fixed[i].c != i + 1 || fixed[i].x != i + 20 ||
            !s_padding_is_zero(&fixed[i]))
            return 6;

    fill(&dynamic, sizeof(dynamic), 0xff);
    for (i = 0; i < count; i++) {
        dynamic[i].c = (char)(i + 4);
        dynamic[i].x = i + 40;
    }
    __builtin_clear_padding(&dynamic);
    for (i = 0; i < count; i++)
        if (dynamic[i].c != i + 4 || dynamic[i].x != i + 40 ||
            !s_padding_is_zero(&dynamic[i]))
            return 7;

    bytes = (unsigned char *)&extended;
#if __SIZEOF_LONG_DOUBLE__ == 16 && __LDBL_MANT_DIG__ == 64
    for (i = 10; i < 16; i++)
        bytes[i] = 0xff;
    __builtin_clear_padding(&extended);
    for (i = 10; i < 16; i++)
        if (bytes[i] != 0)
            return 8;
#else
    {
        unsigned char before[sizeof(long double)];

        for (i = 0; i < (int)sizeof(long double); i++)
            before[i] = bytes[i];
        __builtin_clear_padding(&extended);
        for (i = 0; i < (int)sizeof(long double); i++)
            if (bytes[i] != before[i])
                return 9;
    }
#endif
    if (extended != 1.0L)
        return 10;
    return 0;
}
