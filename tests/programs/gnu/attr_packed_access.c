// EXIT_CODE: 0
// OPT_EQ: all
// Layout numbers prove where a field IS; only running proves the compiler
// reads and writes it there. Every value below round-trips through a
// misaligned member, including a volatile one -- volatile forces the access
// to survive every optimization level, so OPT_EQ is checking real loads.
struct P {
    char a;
    int b;
    long c;
} __attribute__((packed));

struct Q {
    char a;
    double d;
} __attribute__((packed));

/* An array of packed structs: the stride is sizeof, so element k's `b` sits
 * at 13k+1 -- every one of them misaligned differently. */
static struct P tab[4];

static long sum_tab(void)
{
    long acc = 0;
    int i;

    for (i = 0; i < 4; i++)
        acc += tab[i].a + tab[i].b + tab[i].c;
    return acc;
}

int main(void)
{
    struct P p;
    struct Q q;
    volatile struct P *vp = &p;
    int i;

    p.a = 1;
    p.b = 20;
    p.c = 300;
    if (p.a != 1 || p.b != 20 || p.c != 300)
        return 1;

    /* Read-modify-write through a volatile packed member. */
    vp->b = vp->b + 1;
    vp->c = vp->c * 2;
    if (p.b != 21 || p.c != 600)
        return 2;

    q.a = 7;
    q.d = 2.5;
    if (q.a != 7 || q.d != 2.5)
        return 3;

    for (i = 0; i < 4; i++) {
        tab[i].a = (char)i;
        tab[i].b = i * 100;
        tab[i].c = i * 1000;
    }
    /* 0+1+2+3 = 6; 100*(0+1+2+3) = 600; 1000*(0+1+2+3) = 6000. */
    if (sum_tab() != 6606)
        return 4;

    /* Taking the address of a misaligned member is legal; dereferencing the
     * resulting pointer is the program's business, and this one only reads
     * back what it wrote. */
    if (*&p.b != 21)
        return 5;
    return 0;
}
