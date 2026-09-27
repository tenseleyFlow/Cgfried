// FLAGS: -std=gnu17
/* Explicit static mode(TI) initialization uses an exact two-limb image. */
typedef unsigned int u128 __attribute__((mode(TI)));
typedef int i128 __attribute__((mode(TI)));

struct Values {
    unsigned char tag;
    u128 all_ones;
    i128 negative;
};

static u128 zero = 0;
static u128 one = 1;
static u128 all_ones = (u128)-1;
static u128 low_ones = (u128)(unsigned long)-1;
static i128 negative = (i128)-7;
static const u128 const_all_ones = (u128)-1;
static u128 copied_all_ones = const_all_ones;
static u128 array[3] = {1, (u128)-1, 7};
static struct Values record = {3, (u128)-1, (i128)-9};

int main(void)
{
    if (zero != (u128)0 || one != (u128)1)
        return 1;
    if (all_ones != (u128)-1 || negative != (i128)-7)
        return 2;
    if (low_ones != (u128)(unsigned long)-1 ||
        copied_all_ones != (u128)-1)
        return 3;
    if (array[0] != (u128)1 || array[1] != (u128)-1 ||
        array[2] != (u128)7)
        return 4;
    if (record.tag != 3 || record.all_ones != (u128)-1 ||
        record.negative != (i128)-9)
        return 5;
    return 0;
}
