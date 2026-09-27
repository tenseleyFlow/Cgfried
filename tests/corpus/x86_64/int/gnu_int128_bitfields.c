// FLAGS: -std=gnu17
// OPT_EQ: all
// EXIT_CODE: 0
/* GNU permits its 128-bit integer types as bit-field bases. Cgfried keeps TI
 * values address-backed, so this fixture crosses both 64-bit limbs and pins
 * static/runtime initialization, read-modify-write results, packed fields,
 * and reverse scalar storage order without relying on an i128 IR scalar. */
typedef unsigned __int128 u128;
typedef signed __int128 i128;

#define UONE ((u128)1)
#define UBIT(N) (UONE << (N))
#define BE __attribute__((scalar_storage_order("big-endian")))

struct Native {
    u128 low : 3;
    i128 signed_wide : 65;
    u128 high : 60;
};

struct Full {
    u128 value : 128;
};

struct Narrow {
    u128 value : 124;
};

struct Promoted {
    u128 unsigned_five : 5;
    i128 signed_five : 5;
};

struct Packed {
    unsigned char lead : 7;
    u128 value : 124;
    unsigned char tail : 5;
} __attribute__((packed));

struct Reverse {
    u128 value : 124;
    u128 tail : 4;
} BE;

static struct Full static_full = {UBIT(117) | UBIT(64) | 0x35};
static struct Reverse static_reverse = {1, 2};

static int check_static(void)
{
    unsigned char *full = (unsigned char *)&static_full;
    unsigned char *reverse = (unsigned char *)&static_reverse;

    if (sizeof(struct Native) != 16 || _Alignof(struct Native) != 16 ||
        sizeof(struct Full) != 16 || sizeof(struct Reverse) != 16)
        return 1;
    if (static_full.value != (UBIT(117) | UBIT(64) | 0x35))
        return 2;
    if (full[0] != 0x35 || full[8] != 0x01 || full[14] != 0x20)
        return 3;
    if (static_reverse.value != 1 || static_reverse.tail != 2)
        return 4;
    if (reverse[15] != 0x12)
        return 5;
    return 0;
}

static int check_native(u128 wide, i128 signed_wide)
{
    struct Native value = {5, signed_wide, wide};
    u128 assigned;
    i128 signed_assigned;

    if (value.low != 5 || value.signed_wide != -5 ||
        value.high != (UBIT(59) | UBIT(17) | 9))
        return 6;
    assigned = (value.high = UBIT(58) | UBIT(7) | 3);
    if (assigned != (UBIT(58) | UBIT(7) | 3) || value.low != 5 ||
        value.signed_wide != -5 || value.high != assigned)
        return 7;
    signed_assigned = (value.signed_wide = -((i128)UBIT(64)) + 3);
    if (signed_assigned != -((i128)UBIT(64)) + 3 ||
        value.signed_wide != signed_assigned)
        return 8;
    value.high += UBIT(58) | 5;
    if (value.high != UBIT(59) + UBIT(7) + 8)
        return 9;
    if (++value.signed_wide != -((i128)UBIT(64)) + 4)
        return 10;
    if (value.signed_wide++ != -((i128)UBIT(64)) + 4 ||
        value.signed_wide != -((i128)UBIT(64)) + 5)
        return 11;
    return 0;
}

static int check_full(u128 input)
{
    struct Full value = {input};
    u128 before;

    if (value.value != input)
        return 12;
    before = value.value++;
    if (before != input || value.value != input + 1)
        return 13;
    value.value ^= UBIT(100) | 3;
    if (value.value != ((input + 1) ^ (UBIT(100) | 3)))
        return 14;
    return 0;
}

static int check_expression_precision(void)
{
    struct Narrow value = {(UBIT(123) - 1)};

    /* The promoted field type has 124-bit precision. The first addition
     * wraps before the following shift; retaining a fictional 128-bit carry
     * would produce 3 here instead of 1. */
    if (((value.value + value.value) >> 123) != 1)
        return 15;
    return 0;
}

static int check_scalar_promotions(void)
{
    struct Promoted value = {31, -16};

    /* A TI base does not suppress the integer promotions. These fields do
     * their arithmetic as int, then convert the result back through TI to
     * the exact five-bit destination. */
    if ((value.unsigned_five += 3) != 2 || value.unsigned_five != 2)
        return 16;
    if ((value.signed_five += 1) != -15 || value.signed_five != -15)
        return 17;
    if (value.unsigned_five++ != 2 || value.unsigned_five != 3)
        return 18;
    if (++value.signed_five != -14 || value.signed_five != -14)
        return 19;
    if ((value.unsigned_five = 63) != 31 || value.unsigned_five != 31)
        return 20;
    return 0;
}

static int check_packed(u128 input)
{
    struct Packed value = {0x55, input, 0x1b};
    u128 assigned;

    if (sizeof(value) != 17 || value.lead != 0x55 || value.value != input ||
        value.tail != 0x1b)
        return 21;
    assigned = (value.value = UBIT(123) | UBIT(64) | 0x5a);
    if (assigned != (UBIT(123) | UBIT(64) | 0x5a) || value.lead != 0x55 ||
        value.value != assigned || value.tail != 0x1b)
        return 22;
    return 0;
}

static int check_reverse(u128 input)
{
    struct Reverse value = {input, 9};
    unsigned char *bytes = (unsigned char *)&value;

    if (value.value != input || value.tail != 9)
        return 23;
    value.value = UBIT(123) | UBIT(64) | 0x5a;
    if (value.value != (UBIT(123) | UBIT(64) | 0x5a) || value.tail != 9)
        return 24;
    if (bytes[0] != 0x80 || bytes[7] != 0x10 || bytes[15] != 0xa9)
        return 25;
    value.value += 3;
    if (value.value != (UBIT(123) | UBIT(64) | 0x5d) || value.tail != 9)
        return 26;
    return 0;
}

int main(void)
{
    u128 native = UBIT(59) | UBIT(17) | 9;
    u128 packed = UBIT(123) | UBIT(95) | UBIT(64) | 0x123;
    int result = check_static();

    if (result)
        return result;
    result = check_native(native, -5);
    if (result)
        return result;
    result = check_full(UBIT(120) | UBIT(65) | 7);
    if (result)
        return result;
    result = check_expression_precision();
    if (result)
        return result;
    result = check_scalar_promotions();
    if (result)
        return result;
    result = check_packed(packed);
    if (result)
        return result;
    return check_reverse(UBIT(100) | UBIT(64) | 0x33);
}
