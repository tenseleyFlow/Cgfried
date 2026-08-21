// FLAGS: -std=c17
// ENV: CGF_VERIFY_AFTER_EACH=1
// OPT_EQ: all
// EXIT_CODE: 0

static int probe(int *restrict a, int *restrict b)
{
    long i;

    for (i = 20; i >= 1; --i)
        a[i] = 7;
    for (i = 20; i >= 1; --i)
        b[i] = a[i - 1];
    return b[20] + 3 * b[19] + 9 * b[1];
}

int main(void)
{
    int a[21];
    int b[21];
    int i;

    for (i = 0; i <= 20; ++i)
        a[i] = 100;
    return probe(a, b) != 928;
}
