// OPT_EQ: all
// EXIT_CODE: 0
/* GNU permits variably modified members in a block-scope record.  Their byte
 * extents are dynamic, but explicit member alignment, later-member offsets,
 * the containing record's alignment, and its tail-padded size remain one
 * coherent layout contract. */

static int check(int n)
{
    int i;
    unsigned long b_offset = (16UL + 2UL * (unsigned long)n + 3UL) & ~3UL;
    unsigned long total = (b_offset + 4UL * (unsigned long)n + 15UL) & ~15UL;
    struct S {
        char lead;
        __attribute__((aligned(16))) short a[n];
        int b[n];
    } value;

    if (((unsigned long)&value & 15UL) != 0)
        return 1;
    if ((unsigned long)((char *)&value.a[0] - (char *)&value) != 16UL)
        return 2;
    if ((unsigned long)((char *)&value.b[0] - (char *)&value) != b_offset)
        return 3;
    if (__builtin_offsetof(struct S, a) != 16UL ||
        __builtin_offsetof(struct S, b) != b_offset)
        return 4;
    if (sizeof(value) != total)
        return 5;

    value.lead = 7;
    for (i = 0; i < n; i++) {
        value.a[i] = (short)(0x1200 + i);
        value.b[i] = 0x45670000 + i;
    }
    if (value.lead != 7)
        return 6;
    for (i = 0; i < n; i++)
        if (value.a[i] != (short)(0x1200 + i) || value.b[i] != 0x45670000 + i)
            return 7;
    return 0;
}

int main(void)
{
    int result;

    result = check(1);
    if (result)
        return result;
    result = check(2);
    if (result)
        return result;
    result = check(15);
    if (result)
        return result;
    return check(16);
}
