// XFAIL(audit): OPT-H-03 loop unrolling loses a live latch block parameter
// The result of f remains live across g's constant-trip loop after inlining.
// O3 must preserve that predecessor value while unrolling the loop.
int f(int n)
{
    int i = 0;

    while (i < n)
        ++i;
    return i;
}

int g(void)
{
    int i;
    int sum = 0;

    for (i = 0; i < 3; ++i)
        sum = sum * 7 + i;
    return sum;
}

int main(int argc, char **argv)
{
    int a;
    int b;

    (void)argv;
    a = f(argc);
    b = g();
    return ((a ^ b) & 255) != 8;
}
