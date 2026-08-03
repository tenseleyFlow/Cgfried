// FLAGS: -fsafe
// CHECK: 8
#include <stdio.h>

static unsigned shift_by_remainder(unsigned value, unsigned divisor)
{
    return value << (value % divisor);
}

int main(void)
{
    printf("%u\n", shift_by_remainder(4, 3));
    return 0;
}
