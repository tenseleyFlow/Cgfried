// EXIT_CODE: 0
// OPT_EQ: -O0 -O1 -O2 -O3 -Os

struct triple {
    char bytes[3];
};

static int ints[16];
static struct triple triples[16];
static _Atomic(int *) ip;
static _Atomic(struct triple *) tp;

int main(void)
{
    int *old;

    ip = ints + 4;
    old = ip++;
    if (old != ints + 4 || ip != ints + 5)
        return 1;
    if (--ip != ints + 4 || ip != ints + 4)
        return 2;
    if ((ip += 3) != ints + 7 || ip != ints + 7)
        return 3;
    if ((ip -= 2) != ints + 5 || ip != ints + 5)
        return 4;

    tp = triples + 2;
    if (tp++ != triples + 2 || tp != triples + 3)
        return 5;
    if ((tp += 4) != triples + 7 || tp != triples + 7)
        return 6;
    return 0;
}
