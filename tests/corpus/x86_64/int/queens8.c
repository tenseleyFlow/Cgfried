// 8-queens counts 92 solutions: deep recursion + bit fiddling.
// EXIT_CODE: 92
static int count(int row, unsigned cols, unsigned d1, unsigned d2)
{
    unsigned avail, bit;
    int n = 0;
    if (row == 8)
        return 1;
    avail = ~(cols | d1 | d2) & 0xffu;
    while (avail) {
        bit = avail & (0u - avail);
        avail -= bit;
        n += count(row + 1, cols | bit, (d1 | bit) << 1, (d2 | bit) >> 1);
    }
    return n;
}
int main(void)
{
    return count(0, 0, 0, 0);
}
