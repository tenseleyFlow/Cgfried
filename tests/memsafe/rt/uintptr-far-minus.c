#include <stdint.h>

void *malloc(unsigned long);

int main(void)
{
    char *p = malloc(8);
    char *q;

    if (!p)
        return 2;
    q = (char *)((uintptr_t)p - 4096UL);
    return q[0];
}
