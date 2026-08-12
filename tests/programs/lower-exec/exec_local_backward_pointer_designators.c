// EXIT_CODE: 0
// Automatic aggregate relocations must be emitted in object-offset order even
// when source designators move backward through the object.
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
    const char *names[4] = {
        [3] = "three",
        [1] = "one",
    };

    if (names[0] != 0 || names[2] != 0)
        return 1;
    if (!same(names[1], "one"))
        return 2;
    if (!same(names[3], "three"))
        return 3;
    return 0;
}
