// OPT_EQ: all
// EXIT_CODE: 0

static int check(int x)
{
    if (x / 2 != -3)
        return 1;
    if (x % 2 != -1)
        return 2;
    return 0;
}

int main(void)
{
    volatile int x = -7;
    volatile int min = (-2147483647 - 1);

    if (check(x) != 0)
        return 1;
    if (min / min != 1)
        return 2;
    if (min % min != 0)
        return 3;
    return 0;
}
