// MEMORY-class aggregate BY VALUE: the callee's scribbles stay local.
// EXIT_CODE: 0
struct Big {
    long a[6];
};
static long eat(struct Big b)
{
    b.a[0] = 999;
    return b.a[0] + b.a[5];
}
int main(void)
{
    struct Big g;
    int i;
    for (i = 0; i < 6; i++)
        g.a[i] = i;
    if (eat(g) != 1004)
        return 1;
    if (g.a[0] != 0)
        return 2; /* the copy protected us */
    return 0;
}
