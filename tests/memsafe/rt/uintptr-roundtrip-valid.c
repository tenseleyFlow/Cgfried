#include <stdint.h>

void *malloc(unsigned long);
void free(void *);

int main(void)
{
    char *p = malloc(16);
    char *basic, *plus, *minus, *tag, *mask;
    int sum;

    if (!p)
        return 2;
    p[0] = 1;
    p[1] = 2;
    basic = (char *)(uintptr_t)p;
    plus = (char *)((uintptr_t)p + 1UL);
    minus = (char *)((uintptr_t)(p + 1) - 1UL);
    tag = (char *)((uintptr_t)p | 1UL);
    mask = (char *)(((uintptr_t)p | 1UL) & ~15UL);
    sum = basic[0] + plus[0] + minus[0] + tag[0] + mask[0];
    free(p);
    return sum == 7 ? 0 : 1;
}
