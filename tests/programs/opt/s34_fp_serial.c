// FLAGS: -std=c17
// OPT_EQ: all

#include <stdio.h>

int main(void)
{
    double x = 1.0;
    int i;

    for (i = 0; i < 31; i++)
        x = x * 1.0000000000000002 + 0.0000000000000001;
    printf("%.17g\n", x);
    return 0;
}
