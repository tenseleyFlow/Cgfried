#include <stdio.h>

static int a[4][8];
static int b[8][4];
static int c[4][4];

int main(void)
{
    long i, j, k;
    int total = 0;

    for (i = 0; i < 4; i++)
        for (k = 0; k < 8; k++)
            a[i][k] = (int)(i + k + 1);
    for (k = 0; k < 8; k++)
        for (j = 0; j < 4; j++)
            b[k][j] = (int)(k + j + 1);
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            int sum = 0;

            for (k = 0; k < 8; k++)
                sum += a[i][k] * b[k][j];
            c[i][j] = sum;
            total += sum;
        }
    printf("%d\n", total);
    return 0;
}
