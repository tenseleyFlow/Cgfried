#include <stdio.h>
long rsum(int n, ...);
double rmix(int n, ...);
int main(void)
{
    printf("rsum=%ld\n", rsum(9, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L));
    printf("rmix=%.2f\n", rmix(3, 1.25, 2.25, 3.5));
    return 0;
}
