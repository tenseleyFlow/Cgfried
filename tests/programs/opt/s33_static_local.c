// FLAGS: -O2
// OPT_EQ: all
// CHECK: 1 2
int printf(const char *, ...);

static int next_counter(void)
{
    static int counter;

    return ++counter;
}

int main(void)
{
    int first = next_counter();
    int second = next_counter();

    printf("%d %d\n", first, second);
    return first != 1 || second != 2;
}
