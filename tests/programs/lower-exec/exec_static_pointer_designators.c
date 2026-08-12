// EXIT_CODE: 0
// A pointer initializer is wrapped in implicit array-to-pointer and qualifier
// conversions during sema.  Those replacement nodes must retain the
// designator that selects their static-image relocation offset.  QBE's token
// keyword map uses this exact shape; losing the indices left every keyword
// slot null and made its freshly built parser reject all input.
static const char *names[8] = {
    [3] = "three",
    [7] = "seven",
    [1] = "one",
};

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int main(void)
{
    if (names[0] != 0 || names[2] != 0 || names[4] != 0 || names[5] != 0 ||
        names[6] != 0)
        return 1;
    if (!same(names[1], "one"))
        return 2;
    if (!same(names[3], "three"))
        return 3;
    if (!same(names[7], "seven"))
        return 4;
    return 0;
}
