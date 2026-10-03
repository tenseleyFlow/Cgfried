// Reverse-order atomic TI preserves both the logical value and atomic access.
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
// CHECK: OK
#include <stdio.h>

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

struct AtomicRecord {
    unsigned char tag;
    _Atomic(u128) value;
    _Atomic(u128) values[2];
    volatile _Atomic(u128) observed;
    _Atomic(i128) signed_value;
} BE;

static struct AtomicRecord global_record = {
    0x5a, VALUE_A, {VALUE_B, VALUE_C}, VALUE_A,
    -((i128)(UONE << 100) + 17),
};

_Static_assert(__builtin_offsetof(struct AtomicRecord, value) == 16,
               "aligned atomic TI member offset");
_Static_assert(__builtin_offsetof(struct AtomicRecord, values) == 32,
               "atomic TI array offset");
_Static_assert(__builtin_offsetof(struct AtomicRecord, observed) == 64,
               "volatile atomic TI member offset");
_Static_assert(__builtin_offsetof(struct AtomicRecord, signed_value) == 80,
               "signed atomic TI member offset");

static int physical_equals(const void *object, unsigned offset, u128 logical)
{
    const unsigned char *bytes = (const unsigned char *)object + offset;
    unsigned i;

    for (i = 0; i < 16; i++)
        if (bytes[i] != (unsigned char)(logical >> ((15 - i) * 8)))
            return 0;
    return 1;
}

static int check_static(void)
{
    i128 signed_value = -((i128)(UONE << 100) + 17);

    if (global_record.tag != 0x5a || global_record.value != VALUE_A ||
        global_record.values[0] != VALUE_B ||
        global_record.values[1] != VALUE_C ||
        global_record.observed != VALUE_A ||
        global_record.signed_value != signed_value)
        return 1;
    if (!physical_equals(&global_record, 16, VALUE_A) ||
        !physical_equals(&global_record, 32, VALUE_B) ||
        !physical_equals(&global_record, 48, VALUE_C) ||
        !physical_equals(&global_record, 64, VALUE_A) ||
        !physical_equals(&global_record, 80, (u128)signed_value))
        return 2;
    return 0;
}

static int check_runtime(u128 a, u128 b, i128 signed_value)
{
    struct AtomicRecord local = {0x3c, a, {b, a}, b, signed_value};
    u128 result;

    if (local.tag != 0x3c || local.value != a || local.values[0] != b ||
        local.values[1] != a || local.observed != b ||
        local.signed_value != signed_value)
        return 3;
    if (!physical_equals(&local, 16, a) ||
        !physical_equals(&local, 32, b) ||
        !physical_equals(&local, 48, a) ||
        !physical_equals(&local, 64, b) ||
        !physical_equals(&local, 80, (u128)signed_value))
        return 4;

    result = (global_record.value = a);
    if (result != a || global_record.value != a ||
        !physical_equals(&global_record, 16, a))
        return 5;
    result = (global_record.values[0] = b);
    if (result != b || global_record.values[0] != b ||
        !physical_equals(&global_record, 32, b))
        return 6;
    result = (global_record.values[0] += UONE << 72);
    if (result != b + (UONE << 72) || global_record.values[0] != result ||
        !physical_equals(&global_record, 32, result))
        return 7;
    result = global_record.value++;
    if (result != a || global_record.value != a + 1 ||
        !physical_equals(&global_record, 16, a + 1))
        return 8;
    result = (global_record.observed = b);
    if (result != b || global_record.observed != b ||
        !physical_equals(&global_record, 64, b))
        return 9;

    global_record.signed_value = signed_value;
    global_record.signed_value >>= 7;
    if (global_record.signed_value != signed_value >> 7 ||
        !physical_equals(&global_record, 80, (u128)(signed_value >> 7)))
        return 10;
    return 0;
}

int main(void)
{
    int result = check_static();

    if (!result)
        result = check_runtime(VALUE_C, VALUE_B - 19,
                               -((i128)(UONE << 110) + 23));
    if (result)
        return result;
    puts("OK");
    return 0;
}
