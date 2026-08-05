#include <stdio.h>

extern long table[4];
extern long far[513];
long sum_offsets(void);
long *addr_of_third(void);

int main(void)
{
    long s;
    long *p;
    int bad = 0;

    /* 4096 bytes in: one full page past the symbol, which is what
     * distinguishes an addend carried on both halves of the adrp pair from
     * one carried on only the lo12. */
    far[512] = 39;

    s = sum_offsets();
    if (s != 2 + 4 + 39) {
        printf("sum %ld, want %d\n", s, 2 + 4 + 39);
        bad = 1;
    }
    p = addr_of_third();
    if (p != &table[2]) {
        printf("addr %p, want %p\n", (void *)p, (void *)&table[2]);
        bad = 1;
    }
    if (!bad)
        printf("OK\n");
    return bad;
}
