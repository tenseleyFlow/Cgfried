// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 0 1 1 1 0 42 0 16
/* `__builtin_types_compatible_p` and `__builtin_choose_expr`, executed.
 * Every expectation gcc-verified.
 *
 * TWO RULES THAT ARE NOT THE OBVIOUS ONES, both measured:
 *
 *   - TOP-LEVEL QUALIFIERS ARE IGNORED, so `(const int, int)` is 1. The
 *     first implementation used type_qualify(t, 0), which RETURNS THE TYPE
 *     UNCHANGED when asked for zero qualifiers, and answered 0.
 *   - ARRAYS DO NOT DECAY, so `(char *, char[3])` is 0 -- the one context
 *     where an array in a value position stays an array. The builtin asks
 *     about the types as WRITTEN.
 *
 * `calls == 0` is the unevaluated proof: choose_expr must not evaluate the
 * arm it did not select. `sizeof(bound) == 16` proves the result is a
 * CONSTANT EXPRESSION -- it is the bound of a file-scope array, which is
 * the whole reason the builtin exists.
 *
 * The unselected arm IS type-checked, which the sprint file got wrong; the
 * three error fixtures for that live in tests/programs/gnu/. */
extern int printf(const char *, ...);

static int calls;

static int f(void)
{
    calls++;
    return 1;
}

static int bound[__builtin_types_compatible_p(int, int) ? 4 : 1];

int main(void)
{
    int a;
    int arr[3];

    (void)a;
    printf("%d %d %d %d %d %d %d %d %d\n",
           __builtin_types_compatible_p(int, int),
           __builtin_types_compatible_p(int, long),
           __builtin_types_compatible_p(const int, int),
           __builtin_types_compatible_p(__typeof__(a), int),
           __builtin_types_compatible_p(int[3], __typeof__(arr)),
           __builtin_types_compatible_p(char *, char[3]),
           __builtin_choose_expr(1, 42, f()), calls, (int)sizeof(bound));
    return 0;
}
