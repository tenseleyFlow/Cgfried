// C99 division truncates toward zero — both sign mixes.
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): idivl
int main(void)
{
    volatile int a = 7, b = -7, d = 2;
    if (a / d != 3)
        return 1;
    if (b / d != -3)
        return 2;
    if (a % d != 1)
        return 3;
    if (b % d != -1)
        return 4;
    return 0;
}
