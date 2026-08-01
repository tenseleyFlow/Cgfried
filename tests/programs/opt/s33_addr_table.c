// FLAGS: -O2
// OPT_EQ: all
// CHECK: 42
static int add_three(int value)
{
    return value + 3;
}

static int (*table[])(int) = {add_three};

int printf(const char *, ...);

int main(void)
{
    int result = table[0](39);

    printf("%d\n", result);
    return result != 42;
}
