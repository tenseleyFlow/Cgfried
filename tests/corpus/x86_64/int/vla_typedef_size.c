// A variably modified typedef evaluates its bound exactly once, where the
// typedef declaration appears. Every sizeof of that typedef reuses the
// resulting extent, even along different control-flow paths.
// EXIT_CODE: 0
// OPT_EQ: all
static int calls;

static int bound(void)
{
    calls++;
    return 5;
}

static unsigned long size(int pick)
{
    typedef int Row[bound()];

    if (calls != 1)
        return 0;
    if (pick)
        return sizeof(Row);
    return 3 * sizeof(Row);
}

static unsigned long record_size(void)
{
    struct Row {
        char lead;
        int values[bound()];
        char tail;
    };

    if (calls != 1)
        return 0;
    return sizeof(struct Row);
}

static unsigned long packed_record_size(void)
{
    struct Packed {
        char lead;
        int values[bound()];
        char tail;
    } __attribute__((packed));

    if (calls != 1)
        return 0;
    return sizeof(struct Packed);
}

int main(void)
{
    calls = 0;
    if (size(1) != 5UL * sizeof(int) || calls != 1)
        return 1;
    calls = 0;
    if (size(0) != 15UL * sizeof(int) || calls != 1)
        return 2;
    calls = 0;
    if (record_size() != 28 || calls != 1)
        return 3;
    calls = 0;
    if (packed_record_size() != 22 || calls != 1)
        return 4;
    return 0;
}
