// EXIT_CODE: 0
// OPT_EQ: all
// The exemption half of cond_requires_scalar.c, and the half that caught a
// real regression: the first draft of the scalar check rejected `if (arr)`
// and `if (fn)`.
//
// Why they are legal, and why they nearly broke: an array or function
// operand converts to a pointer (6.3.2.1p3/p4), so it IS a valid condition.
// The expression paths decay before checking, so they never see one -- but a
// STATEMENT condition is typed WITHOUT decaying, so `if (arr)` reaches the
// check still an array. A predicate that only accepted arithmetic and
// pointer types refused correct C in exactly that position.
//
// This EXECUTES rather than only compiling, because the interesting claim is
// a runtime one: the address of an array or a function is never null, so
// those conditions are always TRUE. A check that merely let them through
// while lowering got the sense wrong would still pass a compile-only test.
int arr[4] = {0, 0, 0, 0};
void fn(void);
void fn(void)
{
}
int *null_ptr = 0;

static int taken;

int main(void)
{
    int i = 0;
    float fl = 0.0f;
    _Bool b = 0;
    int *p = arr;
    void *vp = arr;
    void (*fp)(void) = fn;
    enum E { E0, E1 } e = E0;

    /* Always true: neither address can be null. */
    if (arr)
        taken |= 1;
    if (fn)
        taken |= 2;
    if (taken != 3)
        return 1;

    /* False conditions of every scalar shape. */
    if (i || fl || b || e || null_ptr)
        return 2;
    if (!p || !vp || !fp || !arr || !fn)
        return 3;

    /* Pointers and arrays through the operators that also require scalars. */
    if (!(p && vp && fp && arr))
        return 4;
    if ((p ? 0 : 1) || (arr ? 0 : 1))
        return 5;

    /* Every INTEGER type may still be switched on, including enum and
     * _Bool -- the stricter rule switch_requires_integer.c pins must not
     * take these with it. */
    switch (i) {
    case 0:
        taken |= 4;
        break;
    default:
        return 9;
    }
    switch (b) {
    case 0:
        taken |= 8;
        break;
    default:
        return 10;
    }
    switch (e) {
    case E0:
        taken |= 16;
        break;
    default:
        return 11;
    }
    if (taken != 31)
        return 12;

    /* Assignment and comma in a condition still work. */
    if ((i = 0))
        return 6;
    if ((fn(), i))
        return 7;
    for (; null_ptr;)
        return 8;
    return 0;
}
