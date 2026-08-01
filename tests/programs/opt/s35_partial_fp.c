// FLAGS: -std=c17
// ENV: CGF_VERIFY_AFTER_EACH=1
// OPT_EQ: all

#include <stdio.h>

int main(void)
{
    double value = 1.0;
    unsigned i;

    for (i = 0; i < 11; i++)
        value = value * 1.0000000000000002 + 0.0000000000000001;
    printf("%.17g\n", value);
    return 0;
}
