// The adjusted pointer retains the inner VLA type. Its stride and sizeof
// use the bound evaluated from the preceding parameter on function entry.
// EXIT_CODE: 0
static int check(int n, int values[][n])
{
    return sizeof values[0] != (unsigned long)n * sizeof(int) ||
           values[1][n - 1] != 37;
}

static int check_nested_type_name(int n,
                                  int values[][sizeof(int[n]) / sizeof(int)])
{
    return values[1][n - 1] != 37;
}

int main(void)
{
    int values[2][5] = {{0}};

    values[1][4] = 37;
    return check(5, values) || check_nested_type_name(5, values);
}
