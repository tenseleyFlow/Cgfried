// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: 3 11 2 5 9 11 CTT
/* GNU statement expressions, executed. Every expectation is gcc-verified.
 *
 * The value is the LAST ITEM and only if that item is an EXPRESSION
 * STATEMENT -- a trailing declaration or a trailing `if` makes the whole
 * thing void, which gcc reports as "void value not ignored". Those two are
 * compile-failure cases and live in tests/programs/gnu/.
 *
 * `CTT` IS THE POINT OF THIS FIXTURE. A `cleanup` variable inside a
 * statement expression runs at ITS OWN `}`, before the enclosing full
 * expression continues -- so the C precedes both T's. Measured against gcc
 * before a line was written, because the plausible alternative (run the
 * cleanups when the enclosing expression ends) also "works" on every test
 * that does not look at ordering. Getting it backwards hands the caller a
 * value read out of storage the cleanup was already given a chance to
 * clobber.
 *
 * The same ordering AST_STMT_RETURN needed for `cleanup`: materialize the
 * value, THEN exit the scope. */
extern int printf(const char *, ...);

static int trace_pos;
static char trace[64];

static void mark(char c)
{
    if (trace_pos < 63)
        trace[trace_pos++] = c;
}
static void note(int *p)
{
    (void)p;
    mark('C');
}
static int tail(int v)
{
    mark('T');
    return v;
}
static int bump(void)
{
    static int n;

    return ++n;
}

int main(void)
{
    int last = ({
        1;
        2;
        3;
    });
    int nested = ({
        int t = ({ 10; });
        t + 1;
    });
    int once = ({
        bump();
        bump();
    });
    long widened = ({ (long)5; });
    int as_arg = ({
        int z = 9;
        z;
    });
    int cleanup_order =
        tail(({
            __attribute__((cleanup(note))) int c = 1;
            5;
        })) +
        tail(6);

    trace[trace_pos] = 0;
    printf("%d %d %d %ld %d %d %s\n", last, nested, once, widened, as_arg,
           cleanup_order, trace);
    return 0;
}
