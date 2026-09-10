// __builtin_prefetch is an optional performance hint, but its address
// expression is evaluated exactly once even when no target instruction is
// emitted. Merely hinting an invalid address must not perform a memory access.
// EXIT_CODE: 0
static int values[4];
static int calls;

static int *address(void)
{
    calls++;
    return values;
}

int main(void)
{
    int *p = values;

    __builtin_prefetch(address(), 1, 3);
    if (calls != 1)
        return 1;

    __builtin_prefetch(p++, 0, 0);
    if (p != values + 1)
        return 2;

    __builtin_prefetch((void *)1);
    return 0;
}
