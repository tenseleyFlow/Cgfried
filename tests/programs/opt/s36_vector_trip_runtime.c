// OPT_EQ: all
// CHECK: vector-trips-ok

extern int puts(const char *);

static int input[1001];
static int out8[8];
static int out9[9];
static int out1000[1000];
static int out1001[1001];

void map8(void)
{
    long i;
    for (i = 0; i < 8; i++)
        out8[i] = input[i] + 7;
}

void map9(void)
{
    long i;
    for (i = 0; i < 9; i++)
        out9[i] = input[i] + 7;
}

void map1000(void)
{
    long i;
    for (i = 0; i < 1000; i++)
        out1000[i] = input[i] + 7;
}

void map1001(void)
{
    long i;
    for (i = 0; i < 1001; i++)
        out1001[i] = input[i] + 7;
}

int main(void)
{
    long i;

    for (i = 0; i < 1001; i++)
        input[i] = (int)(i * 3 + 1);
    map8();
    map9();
    map1000();
    map1001();
    for (i = 0; i < 8; i++)
        if (out8[i] != input[i] + 7)
            return 1;
    for (i = 0; i < 9; i++)
        if (out9[i] != input[i] + 7)
            return 2;
    for (i = 0; i < 1000; i++)
        if (out1000[i] != input[i] + 7)
            return 3;
    for (i = 0; i < 1001; i++)
        if (out1001[i] != input[i] + 7)
            return 4;
    puts("vector-trips-ok");
    return 0;
}
