// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 5/1 99/1 7/1 8 hi hi ok
/* GNU `a ?: b`, executed. gcc-verified.
 *
 * THE CLAIM UNDER TEST IS THE CALL COUNTER, not the result. `a` is
 * evaluated EXACTLY ONCE, and a lowering that evaluates it twice returns
 * the SAME VALUE -- so every test that only checks the result passes on a
 * broken compiler. `calls` is what makes the bug visible, and the
 * true-path case is the discriminating one: on the false path the then-arm
 * never runs, so a double evaluation still reports 1 there. Mutation-
 * verified -- lowering `e->lhs` again in the then-arm prints `5/2 ... 7/2`.
 *
 * `sizeof(1 ?: 2L)` is 8 because the result type comes from the usual
 * arithmetic conversions of BOTH operands, exactly as the three-operand
 * form does; a naive implementation that gives the whole expression the
 * condition's type answers 4.
 *
 * The two array declarations are the constant-expression path: `(5 ?: 0)`
 * must fold to 5 in an integer-constant-expression context, or the bound
 * goes negative and the file does not compile. They assert by EXISTING. */
extern int printf(const char *, ...);

static int calls;
static int val;

static int f(void)
{
    calls++;
    return val;
}

static long g(void)
{
    calls++;
    return 7L;
}

int main(void)
{
    int r;
    long lr;
    const char *s = "hi";

    val = 5;
    calls = 0;
    r = f() ?: 99;
    printf("%d/%d ", r, calls);

    val = 0;
    calls = 0;
    r = f() ?: 99;
    printf("%d/%d ", r, calls);

    calls = 0;
    lr = g() ?: 3;
    printf("%ld/%d ", lr, calls);

    printf("%d ", (int)sizeof(1 ?: 2L));

    printf("%s ", (char *)0 ?: (char *)s);
    printf("%s ", (char *)s ?: (char *)"no");

    {
        int arr[(5 ?: 0) == 5 ? 1 : -1];
        int arr2[(0 ?: 4) == 4 ? 1 : -1];

        (void)arr;
        (void)arr2;
    }
    printf("ok\n");
    return 0;
}
