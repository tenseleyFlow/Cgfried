// OPT_EQ: all
// GNU records containing VLA members use their declaration-time extent for
// every aggregate temporary and assignment, never the one-byte recovery
// layout retained by static type layout.
// EXIT_CODE: 0

static int copy_case(int n)
{
    struct R {
        unsigned char bytes[n];
    } rows[4], result;
    int r, i;

    for (r = 0; r < 4; r++)
        for (i = 0; i < n; i++)
            rows[r].bytes[i] = (unsigned char)(17 * r + i + 1);

    result = ({
        struct R snapshot;
        snapshot = rows[2];
        rows[3] = snapshot;
    });

    for (i = 0; i < n; i++) {
        unsigned char expected = (unsigned char)(35 + i);

        if (result.bytes[i] != expected || rows[3].bytes[i] != expected)
            return 1;
        if (rows[0].bytes[i] != (unsigned char)(i + 1) ||
            rows[1].bytes[i] != (unsigned char)(18 + i))
            return 2;
    }
    return 0;
}

int main(void)
{
    static const int sizes[] = {1, 5, 8, 9, 16, 17};
    int i;

    for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        if (copy_case(sizes[i]) != 0)
            return i + 1;
    return 0;
}
