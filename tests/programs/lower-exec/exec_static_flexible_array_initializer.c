// FLAGS: -std=gnu17
// EXIT_CODE: 0
// GNU static FAM initialization enlarges the emitted definition but not the
// semantic type. The Tail case pins GCC's subtle rule: payload storage is
// appended to sizeof(struct), even though the FAM starts in tail padding.
struct Bytes {
    int tag;
    unsigned char data[];
};

struct Tail {
    long aligner;
    char tag;
    unsigned char data[];
};

static const struct Bytes text = {11, "wx"};
static const struct Bytes sparse = {.tag = 12, .data = {[2] = 99}};
static const struct Tail tail = {13, 14, {15}};

_Static_assert(sizeof text == sizeof(struct Bytes), "FAM changed sizeof");
_Static_assert(sizeof tail == sizeof(struct Tail), "FAM changed sizeof");

static int local_static(void)
{
    static const struct Bytes local = {16, {'a', 'b'}};

    return local.tag == 16 && local.data[0] == 'a' && local.data[1] == 'b';
}

int main(void)
{
    if (text.tag != 11 || text.data[0] != 'w' || text.data[1] != 'x' ||
        text.data[2] != 0)
        return 1;
    if (sparse.tag != 12 || sparse.data[0] != 0 || sparse.data[1] != 0 ||
        sparse.data[2] != 99)
        return 2;
    if (tail.aligner != 13 || tail.tag != 14 || tail.data[0] != 15)
        return 3;
    if (!local_static())
        return 4;
    return 0;
}
