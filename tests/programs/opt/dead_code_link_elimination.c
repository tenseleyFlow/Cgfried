// FLAGS: -std=c17
// OPT_EQ: all
// EXIT_CODE: 0
// ENV: CGF_VERIFY_AFTER_EACH=1

extern void abort(void);
extern void link_error(void);

static int ok;

static void mark_ok(void)
{
    ok = 1;
}

/* `case 1` is a live entry even though the impossible arm in `case 0` also
 * falls into it.  O0 must remove only the unreachable link_error block. */
static void choose(int value)
{
    switch (value) {
    case 0:
        if (0) {
            link_error();
    case 1:
            mark_ok();
        }
    }
}

int main(void)
{
    choose(1);
    if (!ok)
        abort();
    return 0;
}
