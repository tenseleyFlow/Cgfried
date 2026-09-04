// 991014-1 regression: the element count fits size_t, the short-array byte
// size fits size_t, but spelling the record cursor in bits overflows u64.
// The final null-derived member address also forces x86 to materialize a
// ptradd offset that cannot fit its signed 32-bit displacement.
typedef __SIZE_TYPE__ size_type;

#define HUGE_COUNT ((1ULL << (8 * sizeof(size_type) - 2)) - 256)

struct huge_record {
    short bytes[HUGE_COUNT];
    int a;
    int b;
    int c;
    int d;
};

union huge_union {
    int value;
    char bytes[HUGE_COUNT];
};

struct huge_bit_record {
    char prefix[HUGE_COUNT + 1];
    unsigned flag : 1;
};

_Static_assert(sizeof(struct huge_bit_record) == HUGE_COUNT + 4,
               "huge bit-field cursor");

static size_type record_size(void)
{
    return sizeof(struct huge_record);
}

static size_type member_offset(void)
{
    return (size_type) & ((struct huge_record *)0)->a;
}

int main(void)
{
    if (sizeof(union huge_union) != HUGE_COUNT)
        return 1;
    if (record_size() != 2 * HUGE_COUNT + 4 * sizeof(int))
        return 2;
    if (member_offset() != 2 * HUGE_COUNT)
        return 3;
    return 0;
}
