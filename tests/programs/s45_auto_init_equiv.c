// FLAGS: -ftrivial-auto-var-init=zero
// OPT_EQ: -O0 -O2
// CHECK: 0
#include <stddef.h>

int printf(const char *, ...);

struct Pair {
    int left;
    unsigned char right[5];
};

int main(void)
{
    int scalar;
    int *pointer;
    int vector[4];
    struct Pair pair;
    int n = 5;
    unsigned char runtime[n];
    unsigned long total = (unsigned long)scalar;
    int i;

    total |= (unsigned long)(pointer != 0);
    for (i = 0; i < 4; i++)
        total |= (unsigned long)vector[i];
    total |= (unsigned long)pair.left;
    for (i = 0; i < 5; i++) {
        total |= (unsigned long)pair.right[i];
        total |= (unsigned long)runtime[i];
    }
    printf("%lu\n", total);
    return total != 0;
}
