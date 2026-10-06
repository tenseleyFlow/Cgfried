// EXIT_CODE: 0
// OPT_EQ: all
// GCC and Clang treat #pragma pack as lexical translation-unit state.  It
// caps member alignment, selects continuous bit-field allocation, and is not
// equivalent to the one-byte GNU packed attribute when n is 2, 4, 8, or 16.

#define OFF(type, member) __builtin_offsetof(type, member)

struct Natural {
    char c;
    long long value;
};

#pragma pack(push, 4)
struct Pack4 {
    char c;
    long long value;
};

#pragma pack(push, checkpoint, 2)
struct Pack2 {
    char c;
    long long value;
};

#pragma pack(push, 1)
struct Pack1 {
    char c;
    long long value;
};

/* A named pop discards the named checkpoint and every newer entry. */
#pragma pack(pop, checkpoint)
struct Restored4 {
    char c;
    long long value;
};
#pragma pack(pop)

_Pragma("pack(push, 1)") struct MemberAligned {
    char c;
    long long value __attribute__((aligned(8)));
};
struct RecordAligned {
    char c;
    long long value;
} __attribute__((aligned(16)));
union PackedUnion {
    char c;
    long long value;
};
_Pragma("pack(pop)")

#pragma pack(push, 2)
    struct Bits2 {
    unsigned char lead : 7;
    unsigned int value : 26;
    unsigned char tail;
};
#pragma pack(pop)

_Static_assert(sizeof(struct Natural) == 16, "natural size");
_Static_assert(_Alignof(struct Natural) == 8, "natural alignment");
_Static_assert(OFF(struct Natural, value) == 8, "natural offset");
_Static_assert(sizeof(struct Pack4) == 12, "pack-4 size");
_Static_assert(_Alignof(struct Pack4) == 4, "pack-4 alignment");
_Static_assert(OFF(struct Pack4, value) == 4, "pack-4 offset");
_Static_assert(sizeof(struct Pack2) == 10, "pack-2 size");
_Static_assert(_Alignof(struct Pack2) == 2, "pack-2 alignment");
_Static_assert(OFF(struct Pack2, value) == 2, "pack-2 offset");
_Static_assert(sizeof(struct Pack1) == 9, "pack-1 size");
_Static_assert(_Alignof(struct Pack1) == 1, "pack-1 alignment");
_Static_assert(OFF(struct Pack1, value) == 1, "pack-1 offset");
_Static_assert(sizeof(struct Restored4) == 12, "named pop size");
_Static_assert(OFF(struct Restored4, value) == 4, "named pop offset");
_Static_assert(sizeof(struct MemberAligned) == 9, "member cap size");
_Static_assert(_Alignof(struct MemberAligned) == 1, "member cap alignment");
_Static_assert(OFF(struct MemberAligned, value) == 1, "member cap offset");
_Static_assert(sizeof(struct RecordAligned) == 16, "record aligned size");
_Static_assert(_Alignof(struct RecordAligned) == 16,
               "record aligned alignment");
_Static_assert(OFF(struct RecordAligned, value) == 1, "record aligned offset");
_Static_assert(sizeof(union PackedUnion) == 8, "packed union size");
_Static_assert(_Alignof(union PackedUnion) == 1, "packed union alignment");
_Static_assert(sizeof(struct Bits2) == 6, "packed bit-field size");
_Static_assert(_Alignof(struct Bits2) == 2, "packed bit-field alignment");
_Static_assert(OFF(struct Bits2, tail) == 5, "packed bit-field tail");

static struct Pack4 static_pack4 = {'s', 0x123456789abcdefLL};
static struct Bits2 static_bits = {0x55, 0x2345678, 0xa5};
static volatile struct Bits2 volatile_bits;

#pragma pack(push, 2)
static int check_runtime_sized_record(int n)
{
    struct RuntimePack2 {
        char lead;
        long long values[n];
    } value;
    int i;

    if (OFF(struct RuntimePack2, values) != 2 ||
        _Alignof(struct RuntimePack2) != 2 ||
        sizeof(value) != (unsigned long)(2 + 8 * n))
        return 0;
    value.lead = 0x5a;
    for (i = 0; i < n; i++)
        value.values[i] = 0x102030405060708LL + i;
    if (value.lead != 0x5a)
        return 0;
    for (i = 0; i < n; i++)
        if (value.values[i] != 0x102030405060708LL + i)
            return 0;
    return 1;
}
#pragma pack(pop)

int main(void)
{
    struct Pack4 pack4[3];
    struct Pack2 pack2 = {'b', 0x1122334455667788LL};
    struct Pack1 pack1 = {'c', 0x7766554433221100LL};
    union PackedUnion un;
    int i;

    for (i = 0; i < 3; i++) {
        pack4[i].c = (char)('a' + i);
        pack4[i].value = 0x101010101010101LL * (i + 1);
    }
    for (i = 0; i < 3; i++)
        if (pack4[i].c != (char)('a' + i) ||
            pack4[i].value != 0x101010101010101LL * (i + 1))
            return 1;
    if (pack2.c != 'b' || pack2.value != 0x1122334455667788LL)
        return 2;
    if (pack1.c != 'c' || pack1.value != 0x7766554433221100LL)
        return 3;
    if (static_pack4.c != 's' || static_pack4.value != 0x123456789abcdefLL)
        return 4;
    if (static_bits.lead != 0x55 || static_bits.value != 0x2345678 ||
        static_bits.tail != 0xa5)
        return 5;

    volatile_bits.lead = 0x7f;
    volatile_bits.value = 0x3abcdef;
    volatile_bits.tail = 0x5a;
    if (volatile_bits.lead != 0x7f || volatile_bits.value != 0x3abcdef ||
        volatile_bits.tail != 0x5a)
        return 6;

    un.value = 0x102030405060708LL;
    if (un.value != 0x102030405060708LL)
        return 7;
    if (!check_runtime_sized_record(5))
        return 8;
    return 0;
}
