// GNU __builtin_offsetof accepts runtime array indices. They are evaluated
// once, from the inner designator outward, and scale by runtime VLA strides.
// Negative and out-of-range indices remain arithmetic; no object is accessed.
// EXIT_CODE: 0

struct Cell {
    char byte;
    int value;
};

struct Fixed {
    char pad;
    struct Cell cells[4][3];
};

enum {
    CONST_OFFSET = __builtin_offsetof(struct Fixed, cells[2][1].value)
};
typedef char constant_offset_is_ice[CONST_OFFSET == 64 ? 1 : -1];

static int calls;
static int order_bad;

static int pick(int expected, int value)
{
    if (calls != expected)
        order_bad = 1;
    calls++;
    return value;
}

static unsigned long fixed_offset(int i, int j)
{
    return __builtin_offsetof(struct Fixed, cells[i][j].value);
}

static unsigned long effect_offset(int i, int j)
{
    return __builtin_offsetof(struct Fixed,
                              cells[pick(0, i)][pick(1, j)].value);
}

static unsigned long vla_offset(int n, int i, int j)
{
    typedef int Row[n];
    struct Dynamic {
        int tag;
        Row rows[n];
    };

    return __builtin_offsetof(struct Dynamic, rows[i][j]);
}

static int dynamic_is_constant(int i)
{
    return __builtin_constant_p(
        __builtin_offsetof(struct Fixed, cells[i][1].value));
}

int main(void)
{
    if (CONST_OFFSET != 64)
        return 1;
    if (fixed_offset(2, 1) != 64)
        return 2;
    if (fixed_offset(-1, 2) != 0)
        return 3;
    if (fixed_offset(5, 1) != 136)
        return 4;
    if (effect_offset(2, 1) != 64 || calls != 2 || order_bad)
        return 5;
    if (vla_offset(5, 2, 3) != 56)
        return 6;
    if (vla_offset(5, 5, 5) != 124)
        return 7;
    if (dynamic_is_constant(2))
        return 8;
    return 0;
}
