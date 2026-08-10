// OPT_EQ: all
// Aggregate returns: two-eightbyte pair AND memory-class sret.
// EXIT_CODE: 0
struct P {
    long x, y;
};
struct Big {
    long a, b, c, d;
};
struct S1 {
    signed char x;
};
struct S2 {
    short x;
};
struct S4 {
    int x;
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
static struct S1 mk1(void)
{
    struct S1 s = {11};
    return s;
}
static struct S2 mk2(void)
{
    struct S2 s = {2222};
    return s;
}
static struct S4 mk4(void)
{
    struct S4 s = {333333};
    return s;
}
int main(void)
{
    struct P p = mkp(30, 12);
    struct Big b = mkb();
    struct S1 s1 = mk1();
    struct S2 s2 = mk2();
    struct S4 s4 = mk4();

    if (p.x + p.y != 42)
        return 1;
    if (b.a + b.b + b.c + b.d != 10)
        return 2;
    if (s1.x != 11)
        return 3;
    if (s2.x != 2222)
        return 4;
    if (s4.x != 333333)
        return 5;
    return 0;
}
