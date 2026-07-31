// Composite integer workload: sort + verify.
// EXIT_CODE: 0
int main(void)
{
    int v[10] = {9, 3, 7, 1, 8, 2, 6, 0, 5, 4};
    int i, j;
    for (i = 0; i < 10; i++)
        for (j = 0; j + 1 < 10 - i; j++)
            if (v[j] > v[j + 1]) {
                int t = v[j];
                v[j] = v[j + 1];
                v[j + 1] = t;
            }
    for (i = 0; i < 10; i++)
        if (v[i] != i)
            return 1;
    return 0;
}
