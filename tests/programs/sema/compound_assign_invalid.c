// ERROR_EXPECTED: invalid operands to '%='

int f(double a, double b)
{
    a %= b;
    return 0;
}
