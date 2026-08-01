// FLAGS: -O2
// OPT_EQ: all
// CHECK: 123
int printf(const char *, ...);

static int trace;

static void step(int digit)
{
    trace = trace * 10 + digit;
}

static void ordered(void)
{
    step(1);
    step(2);
    step(3);
}

int main(void)
{
    ordered();
    printf("%d\n", trace);
    return trace != 123;
}
