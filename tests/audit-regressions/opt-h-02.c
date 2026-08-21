// RESOLVED(audit): OPT-H-02 descending loop fusion reverses dependence direction
// The second descending loop must observe every value written by the first
// loop. Fusing the bodies at the same descending induction value instead
// reads the next element before its producer iteration has executed.
int f(int *restrict a, int *restrict b)
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
    int result;

    for (i = 0; i <= 20; ++i)
        a[i] = 100;
    result = f(a, b);
    if (result == 928)
        return 0;
    if (result == 1300)
        return 1;
    return 2;
}
