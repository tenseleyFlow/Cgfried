// OPT_EQ: all
// CHECK: scan 3 45

extern int printf(const char *, ...);

static int input[9];
static int output[9];

int main(void)
{
    long i;
    int sum = 0;

    for (i = 0; i < 9; i++)
        input[i] = (int)i + 1;
    for (i = 0; i < 9; i++) {
        sum += input[i];
        output[i] = sum;
    }
    printf("scan %d %d\n", output[1], output[8]);
    return 0;
}
