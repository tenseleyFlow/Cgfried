// FLAGS: -O2
// EXIT_CODE: 42
static long double pick(int take_left, long double left, long double right)
{
    return take_left ? left : right;
}

int main(void)
{
    long double a = pick(1, 7.0L, 11.0L);
    long double b = pick(0, 13.0L, 17.0L);

    return a == 7.0L && b == 17.0L ? 42 : 1;
}
