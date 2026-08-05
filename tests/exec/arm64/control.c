#include <stdio.h>
long sumto(long n);
int classify(int x);
long nested(long a, long b);

static long sumto_ref(long n)
{
    long i, acc = 0;
    for (i = 0; i < n; i++)
        acc += i;
    return acc;
}
static int classify_ref(int x)
{
    switch (x) {
    case -1:
        return -100;
    case 0:
        return 10;
    case 1:
        return 11;
    case 2:
        return 12;
    case 3:
        return 13;
    default:
        return 99;
    }
}
static long nested_ref(long a, long b)
{
    if (a < b)
        return a < 0 ? -1 : 1;
    return a - b;
}

static int fails;
static void chk(const char *w, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: %ld vs %ld\n", w, got, want);
        fails++;
    }
}

int main(void)
{
    long i, j;

    for (i = -3; i < 40; i++)
        chk("sumto", sumto(i), sumto_ref(i));
    for (i = -5; i < 10; i++)
        chk("classify", classify((int)i), classify_ref((int)i));
    for (i = -4; i < 5; i++)
        for (j = -4; j < 5; j++)
            chk("nested", nested(i, j), nested_ref(i, j));
    printf(fails ? "FAILURES %d\n" : "OK\n", fails);
    return fails != 0;
}
