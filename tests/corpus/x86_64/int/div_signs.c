// OPT_EQ: all
// Division sign matrix (INT_MIN/-1 is UB and deliberately absent).
// EXIT_CODE: 0
int main(void)
{
    volatile int p = 13, n = -13, d3 = 3, dn3 = -3;
    if (p / d3 != 4 || p % d3 != 1)
        return 1;
    if (n / d3 != -4 || n % d3 != -1)
        return 2;
    if (p / dn3 != -4 || p % dn3 != 1)
        return 3;
    if (n / dn3 != 4 || n % dn3 != -1)
        return 4;
    return 0;
}
