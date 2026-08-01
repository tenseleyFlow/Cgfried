#include <stdio.h>

static int a[1000];
static int b[1000];

int main(void)
{
    long i;
    long sum = 0;

    for (i = 0; i < 1000; i++)
        a[i] = (int)(i * 3 + 1);
    for (i = 0; i < 1000; i++)
        b[i] = a[i] + 7;
    for (i = 0; i < 1000; i++)
        sum += b[i];
    printf("%ld\n", sum);
    return 0;
}
