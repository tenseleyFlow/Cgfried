#include <stdio.h>

static int a[1001];

int reduce_sum(void)
{
    long i;
    int sum = 0;

    for (i = 0; i < 1001; i++)
        sum += a[i];
    return sum;
}

int main(void)
{
    long i;

    for (i = 0; i < 1001; i++)
        a[i] = (int)(i * 3 + 1);
    printf("%d\n", reduce_sum());
    return 0;
}
