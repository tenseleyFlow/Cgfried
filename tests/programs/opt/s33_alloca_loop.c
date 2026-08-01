// FLAGS: -O2
// OPT_EQ: all
static int touch_vla(int count)
{
    volatile int values[count];

    values[0] = count;
    return values[0];
}

int main(void)
{
    int i;
    int sum = 0;

    for (i = 0; i < 4096; i++)
        sum += touch_vla(1024);
    return sum != 4096 * 1024;
}
