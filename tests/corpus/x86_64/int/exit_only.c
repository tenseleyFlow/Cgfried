// OPT_EQ: all
// Prints nothing: EXIT_CODE alone carries the assertion.
// EXIT_CODE: 77
int main(void)
{
    volatile int x = 7;
    return x * 11;
}
