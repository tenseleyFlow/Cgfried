// FLAGS: -std=c17
// ENV: CGF_VERIFY_AFTER_EACH=1
// OPT_EQ: all
// EXIT_CODE: 0

static volatile int choices[2] = {0, 1};

static int probe(int choose_first)
{
    int a[2] = {0, 0};
    int *p = choose_first ? &a[0] : &a[1];
    int *q = choose_first ? &a[1] : &a[0];

    *p = 11;
    *q = 22;
    return *p;
}

int main(void)
{
    if (probe(choices[0]) != 11)
        return 1;
    if (probe(choices[1]) != 11)
        return 2;
    return 0;
}
