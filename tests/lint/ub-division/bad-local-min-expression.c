// OPT_EQ: all
// EXIT_CODE: 0
int main(void)
{
    const int minimum = -2147483647 - 1;
    return minimum / -1;
}
