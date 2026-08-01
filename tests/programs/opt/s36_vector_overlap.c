// OPT_EQ: all
// CHECK: overlap 1002

extern int printf(const char *, ...);

static int data[1002];

static void copy_shifted(int *dst, int *src)
{
    long i;

    for (i = 0; i < 1001; i++)
        dst[i] = src[i] + 1;
}

int main(void)
{
    data[0] = 1;
    copy_shifted(data + 1, data);
    printf("overlap %d\n", data[1001]);
    return 0;
}
