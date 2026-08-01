// FLAGS: -std=c17
// OPT_EQ: all

static volatile unsigned observed;

int main(void)
{
    unsigned i;

    observed = 0;
    for (i = 0; i < 8; i++)
        observed++;
    return observed == 8 ? 0 : 1;
}
