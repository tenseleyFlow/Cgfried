// OPT_EQ: all
// CHECK: lane-iv 36

extern int printf(const char *, ...);

static long values[9];

int main(void)
{
    long i;
    long sum = 0;

    for (i = 0; i < 9; i++)
        values[i] = i;
    for (i = 0; i < 9; i++)
        sum += values[i];
    printf("lane-iv %ld\n", sum);
    return 0;
}
