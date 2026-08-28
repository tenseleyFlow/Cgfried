// FLAGS: -std=gnu17
// EXIT_CODE: 0
// GNU range initializers are inclusive. A side-effecting value is evaluated
// once for the whole range, even when later designators override some of its
// selected elements.
static int scalar_calls;
static int pair_calls;
static int target;

struct Pair {
    int x;
    int y;
};

static int next_scalar(void)
{
    scalar_calls++;
    return scalar_calls;
}

static int next_pair(void)
{
    pair_calls++;
    return pair_calls;
}

static int inferred[] = {[2 ... 5] = 4};
static int *pointers[3] = {[0 ... 2] = &target, [1] = 0};

int main(void)
{
    int values[5] = {[1 ... 3] = next_scalar(), [2] = 9};
    struct Pair pairs[3] = {[0 ... 2] = {next_pair(), next_pair()}};
    int matrix[2][4] = {[0 ... 1][1 ... 3] = 7};
    int *literal = (int[4]){[1 ... 3] = 5};

    if (sizeof(inferred) / sizeof(inferred[0]) != 6 || inferred[1] != 0 ||
        inferred[2] != 4 || inferred[5] != 4)
        return 1;
    if (pointers[0] != &target || pointers[1] != 0 || pointers[2] != &target)
        return 2;
    if (scalar_calls != 1 || values[0] != 0 || values[1] != 1 ||
        values[2] != 9 || values[3] != 1 || values[4] != 0)
        return 3;
    if (pair_calls != 2 || pairs[0].x != 1 || pairs[2].x != 1 ||
        pairs[0].y != 2 || pairs[2].y != 2)
        return 4;
    if (matrix[0][0] != 0 || matrix[0][1] != 7 || matrix[1][3] != 7)
        return 5;
    if (literal[0] != 0 || literal[1] != 5 || literal[3] != 5)
        return 6;
    return 0;
}
