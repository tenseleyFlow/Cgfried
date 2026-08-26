// A parameter array bound is evaluated on function entry before the array
// parameter adjusts to a pointer for uses in the body.
// EXIT_CODE: 0
static int observe(int n, int values[n++])
{
    (void)values;
    return n;
}

int main(void)
{
    int values[10];

    return observe(10, values) != 11;
}
