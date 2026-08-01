// OPT_EQ: all
// Aggregate returns: two-eightbyte pair AND memory-class sret.
// EXIT_CODE: 0
struct P {
    long x, y;
};
struct Big {
    long a, b, c, d;
};
static struct P mkp(long x, long y)
{
    struct P p;
    p.x = x;
    p.y = y;
    return p;
}
static struct Big mkb(void)
{
    struct Big b;
    b.a = 1;
    b.b = 2;
    b.c = 3;
    b.d = 4;
    return b;
}
int main(void)
{
    struct P p = mkp(30, 12);
    struct Big b = mkb();
    if (p.x + p.y != 42)
        return 1;
    if (b.a + b.b + b.c + b.d != 10)
        return 2;
    return 0;
}
