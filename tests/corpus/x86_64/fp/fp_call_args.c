// OPT_EQ: all
// 9 double args (8 xmm + 1 stack) and a mixed signature, executed.
// EXIT_CODE: 0
static double s9(double a, double b, double c, double d, double e, double f,
                 double g, double h, double i)
{
    return a + b + c + d + e + f + g + h + i;
}
static double mix(int a, double x, int b, double y, int c)
{
    return a + x + b + y + c;
}
int main(void)
{
    if (s9(1, 2, 3, 4, 5, 6, 7, 8, 9) != 45.0)
        return 1;
    if (mix(1, 2.5, 3, 4.5, 5) != 16.0)
        return 2;
    return 0;
}
