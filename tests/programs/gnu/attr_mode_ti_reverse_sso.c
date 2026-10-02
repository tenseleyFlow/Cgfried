// GNU reverse scalar storage order includes ordinary TI members and arrays.
// FLAGS: -std=gnu17 -Wall -Wextra -Wno-scalar-storage-order
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

typedef unsigned __int128 u128;
typedef __int128 i128;

#define BE __attribute__((scalar_storage_order("big-endian")))
#define UONE ((u128)1)
#define VALUE_A                                                               \
    (((u128)0x0011223344556677ULL << 64) | 0x8899aabbccddeeffULL)
#define VALUE_B                                                               \
    (((u128)0xfedcba9876543210ULL << 64) | 0x0123456789abcdefULL)
#define VALUE_C                                                               \
    (((u128)0x1020304050607080ULL << 64) | 0x90a0b0c0d0e0f001ULL)

typedef enum { ENUM_WIDE = VALUE_B } __attribute__((mode(TI))) WideEnum;

struct WideRecord {
    unsigned char tag;
    u128 value;
    i128 signed_value;
    u128 values[2];
    volatile u128 observed;
} BE;

struct PackedRecord {
    unsigned char tag;
    u128 value;
} __attribute__((packed)) BE;

union WideUnion {
    u128 value;
    unsigned char bytes[16];
} BE;

struct EnumRecord {
    WideEnum value;
} BE;

static struct WideRecord global_record = {
    0x5a, VALUE_A, -2, {VALUE_B, VALUE_C}, VALUE_A,
};
static struct PackedRecord global_packed = {0xa5, VALUE_B};
static union WideUnion global_union = {.value = VALUE_C};
static struct EnumRecord global_enum = {ENUM_WIDE};

_Static_assert(__builtin_offsetof(struct WideRecord, value) == 16,
               "aligned TI member offset");
_Static_assert(__builtin_offsetof(struct WideRecord, signed_value) == 32,
               "signed TI member offset");
_Static_assert(__builtin_offsetof(struct WideRecord, values) == 48,
               "TI array member offset");
_Static_assert(__builtin_offsetof(struct WideRecord, observed) == 80,
               "volatile TI member offset");
_Static_assert(sizeof(struct WideRecord) == 96, "TI record size");
_Static_assert(sizeof(struct PackedRecord) == 17, "packed TI record size");

static int physical_equals(const void *object, unsigned offset, u128 logical)
{
    const unsigned char *bytes = (const unsigned char *)object + offset;
    unsigned i;

    for (i = 0; i < 16; i++)
        if (bytes[i] != (unsigned char)(logical >> ((15 - i) * 8)))
            return 0;
    return 1;
}

static int same_bytes(const void *left, const void *right, unsigned count)
{
    const unsigned char *a = left;
    const unsigned char *b = right;
    unsigned i;

    for (i = 0; i < count; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int check_static_images(void)
{
    if (global_record.tag != 0x5a || global_record.value != VALUE_A ||
        global_record.signed_value != -2 || global_record.values[0] != VALUE_B ||
        global_record.values[1] != VALUE_C ||
        global_record.observed != VALUE_A)
        return 1;
    if (!physical_equals(&global_record, 16, VALUE_A) ||
        !physical_equals(&global_record, 32, (u128)-2) ||
        !physical_equals(&global_record, 48, VALUE_B) ||
        !physical_equals(&global_record, 64, VALUE_C) ||
        !physical_equals(&global_record, 80, VALUE_A))
        return 2;
    if (global_packed.tag != 0xa5 || global_packed.value != VALUE_B ||
        !physical_equals(&global_packed, 1, VALUE_B))
        return 3;
    if (global_union.value != VALUE_C ||
        !physical_equals(&global_union, 0, VALUE_C))
        return 4;
    if ((u128)global_enum.value != VALUE_B ||
        !physical_equals(&global_enum, 0, VALUE_B))
        return 5;
    return 0;
}

static int check_runtime(u128 a, i128 s, u128 b)
{
    struct WideRecord initialized = {0x3c, a, s, {b, a}, b};
    struct WideRecord record = {0};
    struct WideRecord copy;
    struct PackedRecord packed = {0x7e, a};
    union WideUnion wide_union = {.value = b};
    struct EnumRecord enum_record = {(WideEnum)a};
    u128 result;

    if (initialized.tag != 0x3c || initialized.value != a ||
        initialized.signed_value != s || initialized.values[0] != b ||
        initialized.values[1] != a || initialized.observed != b)
        return 6;
    if (!physical_equals(&initialized, 16, a) ||
        !physical_equals(&initialized, 32, (u128)s) ||
        !physical_equals(&initialized, 48, b) ||
        !physical_equals(&initialized, 64, a) ||
        !physical_equals(&initialized, 80, b))
        return 7;

    result = (record.value = a);
    if (result != a || record.value != a ||
        !physical_equals(&record, 16, a))
        return 8;
    result = (record.value += 9);
    if (result != a + 9 || record.value != a + 9 ||
        !physical_equals(&record, 16, a + 9))
        return 9;
    result = record.value++;
    if (result != a + 9 || record.value != a + 10 ||
        !physical_equals(&record, 16, a + 10))
        return 10;

    result = (record.values[0] = b);
    if (result != b || record.values[0] != b ||
        !physical_equals(&record, 48, b))
        return 11;
    result = ++record.values[0];
    if (result != b + 1 || record.values[0] != b + 1 ||
        !physical_equals(&record, 48, b + 1))
        return 12;

    record.signed_value = s;
    record.signed_value >>= 7;
    if (record.signed_value != s >> 7 ||
        !physical_equals(&record, 32, (u128)(s >> 7)))
        return 13;

    result = (record.observed = b);
    if (result != b || record.observed != b ||
        !physical_equals(&record, 80, b))
        return 14;
    if (packed.value != a || !physical_equals(&packed, 1, a))
        return 15;
    packed.value = b;
    if (packed.value != b || !physical_equals(&packed, 1, b))
        return 16;
    if (wide_union.value != b || !physical_equals(&wide_union, 0, b))
        return 17;
    if ((u128)enum_record.value != a ||
        !physical_equals(&enum_record, 0, a))
        return 18;

    copy = initialized;
    if (copy.value != a || copy.signed_value != s || copy.values[0] != b ||
        copy.values[1] != a || copy.observed != b ||
        !same_bytes(&copy, &initialized, sizeof(copy)))
        return 19;
    return 0;
}

int main(void)
{
    int result = check_static_images();

    if (result)
        return result;
    return check_runtime(VALUE_A, -((i128)(UONE << 100) + 17), VALUE_B);
}
