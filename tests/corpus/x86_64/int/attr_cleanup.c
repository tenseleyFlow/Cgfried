// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 321|654|987|1122|3|[1]|f=1
/* `cleanup`, EXECUTED rather than inspected.
 *
 * Every line below was run under gcc first and the output copied from it, not
 * predicted. The one expectation that would have been wrong if predicted is
 * the `goto` row: leaving two nested scopes runs those two AT THE GOTO and
 * the enclosing one later at its own end, so the digits interleave with the
 * function's own exit rather than all appearing at the jump.
 *
 * `cleanup` is a lowering problem, not a symbol property, which is why this
 * is an execution fixture and not an ASM_CHECK. There are five ways out of a
 * scope and a compiler can get any one of them wrong while the other four
 * work; each row here is one of them:
 *
 *   order   reverse declaration order within a scope
 *   ret     `return` runs EVERY enclosing scope, innermost first
 *   gotoout `goto` runs only the scopes it leaves
 *   loops   `break` and `continue` both run the loop body's
 *   sw      a `case` body's scope, left by `break`
 *   fi      a for-init variable belongs to the FOR, not the enclosing block
 *   retval  the return VALUE is materialized BEFORE the cleanups run
 *
 * The last is the subtle one and it is observable: `bump` sets its variable
 * to 99 and `f` still returns 1. A lowering that ran cleanups before
 * evaluating the return expression would print f=99 and pass every other row
 * in this file.
 *
 * It runs at every optimization level because a cleanup call is reachable
 * only through the scope-exit edges lowering synthesized -- nothing in the
 * source references it -- so a pass that prunes an edge it thinks is dead
 * would silently drop the call. */
extern int printf(const char *, ...);

static void t(int *p)
{
    printf("%d", *p);
}
#define C __attribute__((cleanup(t)))

static void order(void)
{
    int a C = 1;
    int b C = 2;
    int c C = 3;
    (void)a;
    (void)b;
    (void)c;
}

static void ret(void)
{
    int a C = 4;
    (void)a;
    {
        int b C = 5;
        (void)b;
        {
            int c C = 6;
            (void)c;
            return;
        }
    }
}

static void gotoout(void)
{
    int a C = 7;
    (void)a;
    {
        int b C = 8;
        (void)b;
        {
            int c C = 9;
            (void)c;
            goto done;
        }
    }
done:;
}

static void loops(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        int a C = 1;
        (void)a;
        if (i)
            break;
    }
    for (i = 0; i < 2; i++) {
        int b C = 2;
        (void)b;
        continue;
    }
}

static void sw(int k)
{
    switch (k) {
    case 1: {
        int a C = 3;
        (void)a;
        break;
    }
    default:
        break;
    }
}

/* The for-init variable's scope is the FOR statement: its cleanup runs when
 * the loop ends, before the next statement -- hence `[1]` and not `1[]`. */
static void fi(void)
{
    printf("[");
    for (int i C = 1; i < 1; i++) {
    }
    printf("]");
}

static void bump(int *p)
{
    *p = 99;
}

static int retval(void)
{
    int x __attribute__((cleanup(bump))) = 1;

    return x;
}

int main(void)
{
    order();
    printf("|");
    ret();
    printf("|");
    gotoout();
    printf("|");
    loops();
    printf("|");
    sw(1);
    printf("|");
    fi();
    printf("|f=%d\n", retval());
    return 0;
}
