#include <stdio.h>

static float a[8];
static float b[8];

float dot(void)
{
    long i;
    float sum = 0.0f;

    for (i = 0; i < 8; i++)
        sum += a[i] * b[i];
    return sum;
}

int main(void)
{
    long i;

    for (i = 0; i < 8; i++) {
        a[i] = (float)(i + 1);
        b[i] = 2.0f;
    }
    printf("%.0f\n", (double)dot());
    return 0;
}
