// FLAGS: -O3
// CHECK: vector-runtime-ok
// EXIT_CODE: 0

extern int puts(const char *);

static int input[1001];
static int output[1001];

int main(void)
{
    long i;
    int sum = 0;

    for (i = 0; i < 1001; i++)
        input[i] = i * 3 + 1;
    for (i = 0; i < 1001; i++)
        output[i] = input[i] + 7;
    for (i = 0; i < 1001; i++) {
        if (output[i] != input[i] + 7)
            return 1;
        sum += output[i];
    }
    if (sum != 1509508)
        return 2;
    puts("vector-runtime-ok");
    return 0;
}
