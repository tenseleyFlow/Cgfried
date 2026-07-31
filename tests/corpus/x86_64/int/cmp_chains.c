// setcc+movzx materialization: comparisons AS VALUES.
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): sete
// ASM_CHECK(x86_64-linux-gnu): movzbl
int main(void)
{
    volatile int a = 1, b = 2, c = 3, d = 4;
    int x = (a < b) == (c < d);
    int y = (a > b) + (c < d) + (a == a);
    if (x != 1)
        return 1;
    if (y != 2)
        return 2;
    return 0;
}
