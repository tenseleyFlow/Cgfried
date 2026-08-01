// FLAGS: -std=c17
// ENV: CGF_VERIFY_AFTER_EACH=1
// OPT_EQ: all

static unsigned values[24];

int main(void)
{
    unsigned i;

    for (i = 0; i < 20; i++)
        values[i] = i;
    for (i = 0; i < 20; i++)
        values[i + 1] = values[i] + 1;

    for (i = 0; i < 21; i++)
        if (values[i] != i)
            return 1;
    return 0;
}
