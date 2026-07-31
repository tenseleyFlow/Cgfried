// The PF recipes at runtime: NaN through every relational; -0.0 == 0.0
// while its sign survives negation.
// EXIT_CODE: 0
int main(void)
{
    volatile double n = 0.0 / 0.0, one = 1.0, z = 0.0;
    double nz = -z;
    if (n == one || n < one || n <= one || n > one || n >= one)
        return 1;
    if (!(n != one))
        return 2;
    if (n == n)
        return 3;
    if (nz != z)
        return 4;
    if (1.0 / nz > 0.0)
        return 5;
    return 0;
}
