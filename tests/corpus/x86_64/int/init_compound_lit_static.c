// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 1 2 4 7 9 hi 12
/* A FILE-SCOPE compound literal in a static initializer, in every form.
 * All five were rejected -- "this is not a constant expression" -- where gcc
 * accepts, and they failed for TWO different reasons that had to be found
 * separately:
 *
 *   - the ARRAY forms decay to an address, and constexpr's `eval` had no
 *     compound-literal case at all, so only the explicit `&(T){...}` route
 *     reached the address-constant code. NOT a general hole in implicit
 *     decay: an array VARIABLE and a string literal both came through it
 *     correctly, which is what bounded the search.
 *   - the AGGREGATE form is an image rather than an address, and the
 *     static-initializer check in declare_one validated it as a SCALAR
 *     constant, asking eval() for a value the literal does not have.
 *
 * The line below each is the reason the fix is two changes and not one.
 *
 * Every expectation is gcc-verified. `sizeof(int[]){1,2,3}` is 12, which is
 * also the regression guard for the bound deduction (an undeduced bound
 * reported an incomplete type). */
extern int printf(const char *, ...);

struct S {
    int a, b;
};

static struct S val = (struct S){1, 2};    /* aggregate value  */
static struct S *addr = &(struct S){3, 4}; /* explicit address */
static int *sized = (int[3]){5, 6, 7};     /* array, decays    */
static int *unsized = (int[]){8, 9};       /* bound deduced    */
static const char *chars = (char[]){'h', 'i', 0};

int main(void)
{
    printf("%d %d %d %d %d %s %d\n", val.a, val.b, addr->b, sized[2],
           unsized[1], chars, (int)sizeof(int[]){1, 2, 3});
    return 0;
}
