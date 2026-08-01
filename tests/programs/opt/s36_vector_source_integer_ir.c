// FLAGS: -O3 -emit-ir
// IR_CHECK: vreduce_add v4i32
// IR_CHECK: vreduce_add v4i32
// IR_CHECK: vreduce_and v4i32
// IR_CHECK: vreduce_or v4i32
// IR_CHECK: vreduce_xor v4i32

static int values[1001];

int reduce_add0(void)
{
    long i;
    int value = 0;
    for (i = 0; i < 1001; i++)
        value += values[i];
    return value;
}

int reduce_add17(void)
{
    long i;
    int value = 17;
    for (i = 0; i < 1001; i++)
        value += values[i];
    return value;
}

int reduce_and(void)
{
    long i;
    int value = -1;
    for (i = 0; i < 1001; i++)
        value &= values[i];
    return value;
}

int reduce_or(void)
{
    long i;
    int value = 0;
    for (i = 0; i < 1001; i++)
        value |= values[i];
    return value;
}

int reduce_xor(void)
{
    long i;
    int value = 0;
    for (i = 0; i < 1001; i++)
        value ^= values[i];
    return value;
}
