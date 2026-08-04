// OPT_EQ: all
// EXIT_CODE: 0
int main(void)
{
    int divisor = 0;
    if (0)
        divisor = 1;
    return 12 / divisor;
}
