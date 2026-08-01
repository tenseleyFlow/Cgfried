// OPT_EQ: all
// Array writes + reads: primes below 100 = 25.
// EXIT_CODE: 25
int main(void)
{
    char c[100];
    int i, j, n = 0;
    for (i = 0; i < 100; i++)
        c[i] = 0;
    for (i = 2; i < 100; i++) {
        if (c[i])
            continue;
        n++;
        for (j = i + i; j < 100; j += i)
            c[j] = 1;
    }
    return n;
}
