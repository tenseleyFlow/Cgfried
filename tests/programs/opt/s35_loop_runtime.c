// FLAGS: -std=c17
// ENV: CGF_VERIFY_AFTER_EACH=1
// OPT_EQ: all

static volatile unsigned observed;
static volatile unsigned one = 1;

static unsigned partial9(void)
{
    unsigned sum = 0;
    unsigned i;

    for (i = 0; i < 9; i++)
        sum += one;
    return sum;
}

static unsigned partial10(void)
{
    unsigned sum = 0;
    unsigned i;

    for (i = 0; i < 10; i++)
        sum += one;
    return sum;
}

static unsigned partial11(void)
{
    unsigned sum = 0;
    unsigned i;

    for (i = 0; i < 11; i++)
        sum += one;
    return sum;
}

static unsigned partial12(void)
{
    unsigned sum = 0;
    unsigned i;

    for (i = 0; i < 12; i++)
        sum += one;
    return sum;
}

unsigned unswitched(unsigned n, unsigned flag)
{
    unsigned sum = 0;
    unsigned i;

    for (i = 0; i < n; i++) {
        observed++;
        if (flag) {
            observed += 1;
            sum += i;
        } else {
            observed += 2;
            sum += i + i;
        }
    }
    return sum;
}

static unsigned guarded_divisor(unsigned n, unsigned x, unsigned divisor)
{
    unsigned count = 0;
    unsigned i;

    for (i = 0; i < n; i++)
        if (x / divisor != 0)
            count++;
    return count;
}

int main(int argc, char **argv)
{
    (void)argv;

    if (partial9() != 9 || partial10() != 10 || partial11() != 11 ||
        partial12() != 12)
        return 1;

    observed = 0;
    if (unswitched(9, (unsigned)argc) != 36)
        return 2;
    if (unswitched(11, (unsigned)(argc - 1)) != 110)
        return 3;
    if (observed != 51)
        return 4;

    /* A zero-trip loop must not speculate the otherwise-undefined divide. */
    if (guarded_divisor(0, 1, 0) != 0)
        return 5;
    return 0;
}
