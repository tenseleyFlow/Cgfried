// SSE scalar arithmetic: precision-exact expectations only.
// EXIT_CODE: 0
int main(void)
{
    volatile double a = 0.5, b = 0.25;
    volatile float fa = 1.5f, fb = 0.125f;
    if (a + b != 0.75)
        return 1;
    if (a - b != 0.25)
        return 2;
    if (a * b != 0.125)
        return 3;
    if (a / b != 2.0)
        return 4;
    if (fa * fb != 0.1875f)
        return 5;
    if (-a != -0.5)
        return 6;
    return 0;
}
