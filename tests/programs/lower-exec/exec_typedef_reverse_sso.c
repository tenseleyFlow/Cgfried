// EXIT_CODE: 0
// OPT_EQ: -O0 -O1 -O2 -O3 -Os
#define BE __attribute__((scalar_storage_order("big-endian")))
#define LE __attribute__((scalar_storage_order("little-endian")))

struct Inner {
    unsigned value;
};

struct Payload {
    unsigned direct;
    unsigned short narrow;
    float real;
    unsigned high : 4;
    unsigned low : 12;
    unsigned array[2];
    struct Inner nested;
    struct {
        unsigned anonymous;
    };
};

typedef struct Payload PayloadBE BE;
typedef PayloadBE PayloadAlias;

union Word {
    unsigned value;
    unsigned char bytes[4];
};

typedef union Word WordBE BE;

struct Retagged {
    unsigned value;
};

typedef struct Retagged RetaggedBE BE;
typedef RetaggedBE RetaggedAlias;
static RetaggedAlias retagged_alias_before = {0x11223344u};
typedef RetaggedAlias RetaggedLE LE;

static PayloadBE global = {
    0x11223344u,   0x5566u,       1.0f,
    0xau,          0xbcdu,        {0x778899aau, 0xbbccddefu},
    {0x12345678u}, {0x90abcdefu},
};
static struct Payload native = {
    .direct = 0x11223344u,
    .narrow = 0x5566u,
    .real = 1.0f,
};
static WordBE word = {.value = 0x12345678u};
static RetaggedBE retagged = {0x11223344u};
static RetaggedAlias retagged_alias = {0x11223344u};
static RetaggedLE retagged_le = {0x11223344u};

static int check_payload(const PayloadBE *value)
{
    const unsigned char *bytes = (const unsigned char *)value;

    if (value->direct != 0x11223344u || value->narrow != 0x5566u ||
        value->real != 1.0f || value->high != 0xau || value->low != 0xbcdu)
        return 1;
    if (value->array[0] != 0x778899aau || value->array[1] != 0xbbccddefu ||
        value->nested.value != 0x12345678u || value->anonymous != 0x90abcdefu)
        return 2;
    if (bytes[0] != 0x11 || bytes[1] != 0x22 || bytes[2] != 0x33 ||
        bytes[3] != 0x44 || bytes[4] != 0x55 || bytes[5] != 0x66)
        return 3;
    if (bytes[8] != 0x3f || bytes[9] != 0x80 || bytes[10] != 0x00 ||
        bytes[11] != 0x00 || bytes[12] != 0xab || bytes[13] != 0xcd)
        return 4;
    /* GCC applies a typedef-attached order only to direct scalar and
     * bit-field members.  Direct arrays, nested records, and promoted
     * anonymous-record members retain their own native order. */
    if (bytes[16] != 0xaa || bytes[17] != 0x99 || bytes[18] != 0x88 ||
        bytes[19] != 0x77 || bytes[24] != 0x78 || bytes[25] != 0x56 ||
        bytes[28] != 0xef || bytes[29] != 0xcd)
        return 5;
    return 0;
}

static int check_runtime(unsigned value)
{
    PayloadAlias local = {
        value,  (unsigned short)(value >> 16), 1.0f,         0xau,
        0xbcdu, {value + 1u, value + 2u},      {value + 3u}, {value + 4u},
    };
    const unsigned char *bytes = (const unsigned char *)&local;

    if (local.direct != value || local.narrow != (unsigned short)(value >> 16))
        return 6;
    if (bytes[0] != 0x11 || bytes[1] != 0x22 || bytes[2] != 0x33 ||
        bytes[3] != 0x44 || bytes[4] != 0x11 || bytes[5] != 0x22)
        return 7;
    local.direct += 2u;
    local.real *= 2.0f;
    local.high++;
    local.low -= 2u;
    if (local.direct != value + 2u)
        return 81;
    if (local.real != 2.0f)
        return 82;
    if (local.high != 0xbu)
        return 83;
    if (local.low != 0xbcbu)
        return 84;
    if (bytes[0] != 0x11 || bytes[1] != 0x22 || bytes[2] != 0x33 ||
        bytes[3] != 0x46 || bytes[8] != 0x40 || bytes[9] != 0x00 ||
        bytes[12] != 0xbb || bytes[13] != 0xcb)
        return 9;
    return 0;
}

int main(void)
{
    const unsigned char *native_bytes = (const unsigned char *)&native;
    const unsigned char *word_bytes = (const unsigned char *)&word;
    const unsigned char *retagged_bytes = (const unsigned char *)&retagged;
    const unsigned char *retagged_alias_bytes =
        (const unsigned char *)&retagged_alias;
    const unsigned char *retagged_alias_before_bytes =
        (const unsigned char *)&retagged_alias_before;
    const unsigned char *retagged_le_bytes =
        (const unsigned char *)&retagged_le;
    int result = check_payload(&global);

    if (result)
        return result;
    if (native_bytes[0] != 0x44 || native_bytes[1] != 0x33 ||
        native_bytes[4] != 0x66 || native_bytes[5] != 0x55)
        return 10;
    if (word.value != 0x12345678u || word_bytes[0] != 0x12 ||
        word_bytes[1] != 0x34 || word_bytes[2] != 0x56 || word_bytes[3] != 0x78)
        return 11;
    if (retagged.value != 0x11223344u || retagged_bytes[0] != 0x11 ||
        retagged_bytes[1] != 0x22 || retagged_bytes[2] != 0x33 ||
        retagged_bytes[3] != 0x44)
        return 12;
    if (retagged_alias.value != 0x11223344u ||
        retagged_alias_bytes[0] != 0x44 || retagged_alias_bytes[1] != 0x33 ||
        retagged_alias_bytes[2] != 0x22 || retagged_alias_bytes[3] != 0x11 ||
        retagged_alias_before.value != 0x11223344u ||
        retagged_alias_before_bytes[0] != 0x44 ||
        retagged_alias_before_bytes[1] != 0x33 ||
        retagged_alias_before_bytes[2] != 0x22 ||
        retagged_alias_before_bytes[3] != 0x11 ||
        retagged_le.value != 0x11223344u || retagged_le_bytes[0] != 0x44 ||
        retagged_le_bytes[1] != 0x33 || retagged_le_bytes[2] != 0x22 ||
        retagged_le_bytes[3] != 0x11)
        return 13;
    return check_runtime(0x11223344u);
}
