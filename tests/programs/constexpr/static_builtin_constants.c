// Compiler-owned constant builtins must work in static initializers and in
// __builtin_choose_expr's integer-constant selector.
// FLAGS: -std=gnu17
// EXIT_CODE: 0

struct Pair {
    int left;
    int right;
};

static int unknown_source;
static int known = __builtin_constant_p(6 * 7);
static int unknown = __builtin_constant_p(unknown_source);
static int chosen = __builtin_choose_expr(!__builtin_constant_p(unknown_source),
                                          17, unknown_source);
static int *member = &((struct Pair){.left = 1, .right = 42}).right;
static double infinity = __builtin_inf();
static double nan_lower = __builtin_nan("0x1");
static double nan_upper = __builtin_nan("0X1");

int main(void)
{
    if (known != 1 || unknown != 0 || chosen != 17)
        return 1;
    if (*member != 42)
        return 2;
    if (!(infinity > 1e308))
        return 3;
    if (!(nan_lower != nan_lower) || !(nan_upper != nan_upper))
        return 4;
    if (__builtin_memcmp(&nan_lower, &nan_upper, sizeof(nan_lower)) != 0)
        return 5;
    return 0;
}
