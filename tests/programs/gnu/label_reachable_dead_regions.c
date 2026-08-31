// FLAGS: -std=gnu17
// OPT_EQ: all
// EXIT_CODE: 0
// ENV: CGF_VERIFY_AFTER_EACH=1

/* A label in a GNU statement expression has function scope even when its
 * containing loop sits under if (0).  The later goto enters after the first
 * increment, then executes the remaining loop iterations. */
static int entered_stmt_expr(void)
{
    int j = 1;

    return ({
        int count = 0;

        if (0) {
            while (j--) {
                count += 10;
entered:
                count++;
            }
        }
        if (j >= 0)
            goto entered;
        count;
    });
}

/* The false arm must still be lowered well enough to retain the goto edge,
 * even though the condition makes it the arm taken at runtime.  Lowering the
 * statement expression ends that arm's block; its enclosing conditional must
 * continue structurally in an orphan block rather than append after the
 * terminator. */
static int leaves_stmt_expr(void)
{
    0 ? (void)0 : ({ goto escaped; });
    return 1;

escaped:
    return 7;
}

static int cleanups;

static void note_cleanup(int *value)
{
    cleanups += *value;
}

/* A terminating edge already leaves the statement-expression scope.  Its
 * cleanup must run on the goto, but never again while the dead continuation
 * finishes its enclosing expression structurally. */
static int leaves_stmt_expr_with_cleanup(void)
{
    ({
        int value __attribute__((cleanup(note_cleanup))) = 3;

        goto cleaned;
    });
    return 1;

cleaned:
    return cleanups;
}

int main(void)
{
    return entered_stmt_expr() != 12 || leaves_stmt_expr() != 7 ||
           leaves_stmt_expr_with_cleanup() != 3;
}
