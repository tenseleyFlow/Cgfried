// OPT_EQ: all
// Loop-carried pair: fib(11) = 89.
// EXIT_CODE: 89
int main(void)
{
    int a = 0, b = 1, i;
    for (i = 0; i < 11; i++) {
        int t = a + b;
        a = b;
        b = t;
    }
    return a;
}
