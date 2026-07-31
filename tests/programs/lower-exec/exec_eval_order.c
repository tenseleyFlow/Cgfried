// The §1 evaluation-order law, pinned as EXECUTION: cgf is strict
// left-to-right, so f() runs before g() and the result is 1 - 10 = -9;
// h's exit code is the two's-complement byte 247.
// EXIT_CODE: 247
int i = 0;
int f(void)
{
    return ++i;
}
int g(void)
{
    return i *= 10;
}
int h(int a, int b)
{
    return a - b;
}
int main(void)
{
    return h(f(), g());
}
