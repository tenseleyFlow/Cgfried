// while/branch shapes: 27 takes 111 steps to reach 1.
// EXIT_CODE: 111
int main(void)
{
    unsigned long n = 27;
    int steps = 0;
    while (n != 1) {
        n = (n % 2) ? 3 * n + 1 : n / 2;
        steps++;
    }
    return steps;
}
