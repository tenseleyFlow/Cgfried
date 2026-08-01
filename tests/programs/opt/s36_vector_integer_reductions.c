// OPT_EQ: all
// CHECK: reductions

extern int printf(const char *, ...);

static int values[1001];

int reduce_add0(void)
{
    long i;
    int add0 = 0;

    for (i = 0; i < 1001; i++)
        add0 += values[i];
    return add0;
}

int reduce_add17(void)
{
    long i;
    int add17 = 17;

    for (i = 0; i < 1001; i++)
        add17 += values[i];
    return add17;
}

int reduce_and(void)
{
    long i;
    int band = -1;

    for (i = 0; i < 1001; i++)
        band &= values[i];
    return band;
}

int reduce_or(void)
{
    long i;
    int bor = 0;

    for (i = 0; i < 1001; i++)
        bor |= values[i];
    return bor;
}

int reduce_xor(void)
{
    long i;
    int bxor = 0;

    for (i = 0; i < 1001; i++)
        bxor ^= values[i];
    return bxor;
}

int main(void)
{
    long i;

    for (i = 0; i < 1001; i++)
        values[i] = (int)(i * 3 + 1);
    printf("reductions %d %d %d %d %d\n", reduce_add0(), reduce_add17(),
           reduce_and(), reduce_or(), reduce_xor());
    return 0;
}
