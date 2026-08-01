// FLAGS: -std=c17
// OPT_EQ: all

static int guarded(int n, int divisor, const int *ptr)
{
    int acc = 0;
    int i;

    for (i = 0; i < n; i++)
        acc += 120 / divisor + *ptr;
    return acc;
}

static int conditional_store(int n)
{
    int value = 9;
    int i;

    for (i = 0; i < n; i++)
        if (i & 1)
            value = i;
    return value;
}

int main(void)
{
    int seven = 7;

    if (guarded(0, 0, (const int *)0) != 0)
        return 1;
    if (guarded(3, 5, &seven) != 93)
        return 2;
    if (conditional_store(1) != 9)
        return 3;
    return 0;
}
