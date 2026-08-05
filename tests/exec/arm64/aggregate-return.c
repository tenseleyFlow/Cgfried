#include <stdio.h>

struct Pair {
    long x, y;
};
struct Big {
    long a, b, c, d;
};

struct Pair mkpair(long x, long y);
struct Big mkbig(long seed);

/* The arguments are deliberately small integers. If the callee mistakes the
 * hidden return pointer for its first parameter it stores THROUGH one of
 * them, so the failure mode is a wild write rather than a wrong answer --
 * the original bug stored through the address 30. */
int main(void)
{
    struct Pair p = mkpair(30, 12);
    struct Big b = mkbig(100);
    int bad = 0;

    /* The halves must arrive in x0:x1, in that order. */
    if (p.x != 30 || p.y != 12) {
        printf("pair %ld %ld, want 30 12\n", p.x, p.y);
        bad = 1;
    }
    /* The destination came from x8, and the seed was still in x0 -- x8
     * consumes none of the argument registers. */
    if (b.a != 100 || b.b != 101 || b.c != 102 || b.d != 103) {
        printf("big %ld %ld %ld %ld, want 100 101 102 103\n", b.a, b.b, b.c,
               b.d);
        bad = 1;
    }
    if (!bad)
        printf("OK\n");
    return bad;
}
