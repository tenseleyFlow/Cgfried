// OPT_EQ: all
// EXIT_CODE: 0
int main(void)
{
    int n = -13, d = 3;
    double nan = 0.0 / 0.0;
    /* INT_MIN / -1 and 1 / 0 are examples in comments, not behavior. */
    return n / d != -4 || n % d != -1 || nan == nan;
}
