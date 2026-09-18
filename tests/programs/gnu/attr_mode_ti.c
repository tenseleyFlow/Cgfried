// FLAGS: -std=gnu17
/* The exhaustive full-width arithmetic oracle lives in
 * tests/fixtures/gnu/mode_ti_abi.c.
 * This small fixture keeps the ordinary program runner's GNU lane aware of
 * the type, including its two-register call/return ABI and a deliberately
 * unaligned packed-member source. */
typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((__mode__(__TI__)));

struct __attribute__((packed)) Packed {
    unsigned char tag;
    u128 value;
};

static u128 combine(unsigned long before, u128 a, u128 b,
                    unsigned long after)
{
    return a + b + before + after;
}

int main(void)
{
    struct Packed p;
    u128 array[2];
    i128 negative = (i128)-7;
    volatile u128 observed = (u128)5;
    volatile u128 index = (u128)1;
    unsigned long narrow = 7;
    int vla[observed];

    if (sizeof(u128) != 16 || _Alignof(u128) != 16)
        return 1;
    p.tag = 3;
    p.value = (u128)19;
    p.value++;
    array[0] = 0;
    array[index] = combine(1, p.value, (u128)20, 2);
    if (&array[index] != index + array)
        return 2;
    if (array[1] != (u128)43)
        return 3;
    +observed;
    (u128)observed;
    if (observed != (u128)5)
        return 4;
    observed, array[0];
    if ((observed ?: (u128)9) != (u128)5)
        return 5;
    narrow += (u128)5;
    narrow <<= observed;
    vla[0] = 11;
    if (narrow != 384 || vla[0] != 11)
        return 6;
    if (negative >= 0 || !negative || (unsigned long)negative != -7ul)
        return 7;
    return 0;
}
