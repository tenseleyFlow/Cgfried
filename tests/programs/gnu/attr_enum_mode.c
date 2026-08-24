// FLAGS: -std=gnu17
// EXIT_CODE: 0
/* GNU enum modes preserve enum identity while selecting a target integer
 * representation. Exercise both signedness choices, every implemented fixed
 * width, a definition-bound mode, and a mode attached to an existing tag. */
typedef enum { U8_ZERO, U8_MAX = 255 } __attribute__((mode(QI))) U8;
typedef enum { I8_MIN = -128, I8_MAX = 127 } __attribute__((mode(QI))) I8;
typedef enum { U16_ZERO, U16_MAX = 65535 } __attribute__((mode(HI))) U16;
typedef enum { U32_ZERO, U32_MAX = 0xFFFFFFFFu } __attribute__((mode(SI))) U32;
typedef enum { U64_ZERO, U64_ONE } __attribute__((mode(DI))) U64;

typedef enum DirectTag {
    DIRECT_ZERO,
    DIRECT_ONE
} __attribute__((mode(QI))) Direct;
typedef enum SuffixTag {
    SUFFIX_ZERO,
    SUFFIX_ONE
} Suffix __attribute__((mode(QI)));
__attribute__((mode(QI))) enum LeadingTag {
    LEADING_ZERO,
    LEADING_ONE
} leading_value;

enum ExistingTag { EXISTING_ZERO, EXISTING_ONE };
typedef enum ExistingTag __attribute__((mode(HI))) ExistingWide;

_Static_assert(sizeof(U8) == 1, "QI enum size");
_Static_assert(sizeof(I8) == 1, "signed QI enum size");
_Static_assert(sizeof(U16) == 2, "HI enum size");
_Static_assert(sizeof(U32) == 4, "SI enum size");
_Static_assert(sizeof(U64) == 8, "DI enum size");
_Static_assert(sizeof(enum DirectTag) == 1, "definition-bound enum mode");
_Static_assert(sizeof(enum SuffixTag) == 4, "suffix leaves tag unchanged");
_Static_assert(sizeof(Suffix) == 1, "suffix changes only typedef");
_Static_assert(sizeof(enum LeadingTag) == 4, "leading leaves tag unchanged");
_Static_assert(sizeof(leading_value) == 1, "leading changes declaration");
_Static_assert(sizeof(enum ExistingTag) == 4, "existing tag unchanged");
_Static_assert(sizeof(ExistingWide) == 2, "attributed enum view");

static U8 round_u8(U8 value)
{
    return value;
}
static I8 round_i8(I8 value)
{
    return value;
}
static U16 round_u16(U16 value)
{
    return value;
}
static U32 round_u32(U32 value)
{
    return value;
}

int main(void)
{
    if ((unsigned int)round_u8((U8)U8_MAX) != 255u)
        return 1;
    if ((int)round_i8((I8)I8_MIN) != -128)
        return 2;
    if ((unsigned int)round_u16((U16)U16_MAX) != 65535u)
        return 3;
    if ((unsigned long)round_u32((U32)U32_MAX) != 0xFFFFFFFFul)
        return 4;
    return 0;
}
