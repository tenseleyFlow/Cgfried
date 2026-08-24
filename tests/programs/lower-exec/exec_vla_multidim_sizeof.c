// A fixed outer dimension does not make a multidimensional array constant
// sized when an inner dimension is variable.
// EXIT_CODE: 0
static int check(int n)
{
    int values[3][n];
    int *last = values[2];

    values[2][n - 1] = 41;
    if (sizeof values != 3UL * (unsigned long)n * sizeof(int))
        return 1;
    return last[n - 1] != 41;
}

int main(void)
{
    return check(7);
}
