// GNU TI enums retain full-width enumerators, arithmetic, storage, and ABI.
// FLAGS: -std=gnu17 -Wall -Wextra
// WARN_COUNT: 0
// EXIT_CODE: 0
// OPT_EQ: all

typedef unsigned __int128 u128;
typedef __int128 i128;
typedef __builtin_va_list va_list;

#define UONE ((u128)1)
#define BIT100 (UONE << 100)
#define BIT120 (UONE << 120)

typedef enum {
    U_ZERO,
    U_BIG = BIT100 + 9,
    U_NEXT
} __attribute__((mode(TI))) UEnum;

typedef enum {
    S_LOW = -((i128)BIT100 + 17),
    S_NEXT
} __attribute__((mode(TI))) SEnum;

typedef enum {
    CARRY_PREV = (UONE << 64) - 1,
    CARRY_NEXT
} __attribute__((mode(TI))) CarryEnum;

typedef enum { TINY_ZERO, TINY_ONE } __attribute__((mode(TI))) TinyEnum;

enum AutoUnsigned { AUTO_BIG = BIT120 + 5 };
enum AutoSigned { AUTO_LOW = -((i128)BIT100 + 23) };

enum Direct { DIRECT_ZERO, DIRECT_ONE } __attribute__((mode(TI)));
enum Base { BASE_ZERO, BASE_ONE };
typedef enum Base __attribute__((mode(TI))) BaseWide;

struct Values {
    unsigned char tag;
    UEnum value;
    SEnum signed_value;
};

struct Bits {
    UEnum value : 101;
    SEnum signed_value : 102;
};

static UEnum static_value = U_BIG;
static UEnum static_cast = (UEnum)(BIT120 + 21);
static struct Values record = {3, U_NEXT, S_LOW};
static UEnum array[2] = {U_ZERO, U_BIG};
static _Atomic(UEnum) atomic_value;

_Static_assert(sizeof(UEnum) == 16, "unsigned TI enum size");
_Static_assert(_Alignof(UEnum) == _Alignof(u128), "TI enum alignment");
_Static_assert(sizeof(SEnum) == 16, "signed TI enum size");
_Static_assert(sizeof(enum Direct) == 16, "definition-bound mode");
_Static_assert(sizeof(enum AutoUnsigned) == 16, "inferred unsigned TI enum");
_Static_assert(sizeof(enum AutoSigned) == 16, "inferred signed TI enum");
_Static_assert(sizeof(enum Base) == 4, "ordinary tag remains unchanged");
_Static_assert(sizeof(BaseWide) == 16, "attributed enum view");
_Static_assert(sizeof(array) == 32, "TI enum array stride");
_Static_assert(__builtin_offsetof(struct Values, value) == 16,
               "TI enum member alignment");
_Static_assert(__builtin_types_compatible_p(UEnum, u128),
               "unsigned compatible type");
_Static_assert(__builtin_types_compatible_p(SEnum, i128),
               "signed compatible type");
_Static_assert(__builtin_types_compatible_p(__typeof__(+((UEnum)0)), u128),
               "unsigned enum promotion");
_Static_assert(__builtin_types_compatible_p(__typeof__(+((SEnum)0)), i128),
               "signed enum promotion");
_Static_assert(__builtin_types_compatible_p(__typeof__(TINY_ZERO), int),
               "all-int enumerators remain int");
_Static_assert(__builtin_types_compatible_p(__typeof__(U_BIG), u128),
               "wide enumerator uses the enum representation");

static UEnum round_unsigned(UEnum value)
{
    return value;
}

static SEnum round_signed(SEnum value)
{
    return value;
}

static UEnum take_enum(int tag, ...)
{
    va_list ap;
    UEnum value;

    __builtin_va_start(ap, tag);
    value = __builtin_va_arg(ap, UEnum);
    __builtin_va_end(ap);
    return value;
}

static int classify(UEnum value)
{
    switch (value) {
    case U_ZERO:
        return 1;
    case U_BIG:
        return 2;
    case U_NEXT:
        return 3;
    }
    return 0;
}

int main(void)
{
    UEnum value = (UEnum)(BIT120 + BIT100 + 27);
    SEnum signed_value = (SEnum) - ((i128)BIT100 + 17);
    struct Bits bits = {0};
    UEnum old;

    if ((u128)U_BIG != BIT100 + 9 || (u128)U_NEXT != BIT100 + 10)
        return 1;
    if ((i128)S_LOW != -((i128)BIT100 + 17) ||
        (i128)S_NEXT != -((i128)BIT100 + 16))
        return 2;
    if ((u128)CARRY_PREV != (UONE << 64) - 1 || (u128)CARRY_NEXT != UONE << 64)
        return 15;
    if ((u128)AUTO_BIG != BIT120 + 5 || (i128)AUTO_LOW != -((i128)BIT100 + 23))
        return 14;
    if ((u128)round_unsigned(value) != BIT120 + BIT100 + 27)
        return 3;
    if ((i128)round_signed(signed_value) != -((i128)BIT100 + 17))
        return 4;
    if ((u128)take_enum(0, value) != (u128)value)
        return 5;
    if ((u128)static_value != BIT100 + 9 || (u128)static_cast != BIT120 + 21)
        return 6;
    if (record.tag != 3 || (u128)record.value != BIT100 + 10 ||
        (i128)record.signed_value != -((i128)BIT100 + 17))
        return 7;
    if ((u128)array[0] != 0 || (u128)array[1] != BIT100 + 9)
        return 8;

    value += 5;
    value ^= (UEnum)(UONE << 64);
    if ((u128)value != BIT120 + BIT100 + (UONE << 64) + 32)
        return 9;
    signed_value >>= 7;
    if ((i128)signed_value != (-((i128)BIT100 + 17) >> 7))
        return 10;

    bits.value = (UEnum)(BIT100 + 31);
    bits.signed_value = (SEnum) - ((i128)BIT100 + 7);
    if ((u128)bits.value != BIT100 + 31 ||
        (i128)bits.signed_value != -((i128)BIT100 + 7))
        return 11;
    if (classify(U_BIG) != 2 || classify(U_NEXT) != 3)
        return 12;

    atomic_value = (UEnum)(BIT100 + 41);
    old = atomic_value++;
    if ((u128)old != BIT100 + 41 || (u128)atomic_value != BIT100 + 42)
        return 13;
    return 0;
}
