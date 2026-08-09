// OPT_EQ: all
// EXIT_CODE: 0
// CHECK: ABCD|AB.E
/* THE HAZARD gcc does not diagnose, pinned because we match it.
 *
 * A `goto` INTO a cleanup scope, landing past the declaration, is accepted
 * silently by gcc -- and the cleanup then runs at scope exit on a variable
 * that was never initialized. Nothing warns: not gcc, not us, not
 * -Wuninitialized, because the read happens inside the cleanup function
 * where the variable arrives as an ordinary pointer.
 *
 * The fixture prints a FIXED marker rather than the variable's value. That is
 * not squeamishness: reading it is undefined behaviour and the byte is
 * genuinely arbitrary -- an early draft of this measurement had gcc print 0
 * and cgf print 2, which is not a divergence, just two arbitrary stack
 * residues. Pinning the value would make this fixture fail for the wrong
 * reason on a different day, on a different allocator, or at a different
 * optimization level.
 *
 * CLANG REJECTS THIS PROGRAM ("cannot jump from this goto statement to its
 * label"), treating a cleanup scope the way C++ treats one with a destructor.
 * gcc compiles it. We follow gcc, deliberately: `cleanup` is a GNU extension
 * and gcc defines it, so a program that builds with gcc must build here. The
 * disagreement is recorded rather than resolved -- if this compiler ever
 * grows the diagnostic, it is clang's rule being adopted, not a bug fix.
 *
 * What IS well-defined and worth pinning: the cleanup runs exactly once, on
 * the way out of the scope the label sits in, and the jump itself compiles.
 * A lowering that registered cleanups only at the declaration's own
 * execution -- rather than for the scope -- would print ABD, and one that
 * ran it at the goto as well would print ABCD with an extra C. */
extern int printf(const char *, ...);

static void mark(int *p)
{
    (void)p;
    printf("C");
}

static void dot(int *p)
{
    (void)p;
    printf(".");
}

static void jump_in(void)
{
    goto inside;
    {
        int a __attribute__((cleanup(mark))) = 1;

        (void)a;
    inside:
        printf("B");
    }
}

/* THE SECOND ROW, and it is the one that found a bug.
 *
 * Same jump, but now the scope the goto STARTS in has a cleanup of its own.
 * `outer` must run once, at the end of the function -- not at the goto. A
 * lowering that stops its walk at the label's own compound cannot express
 * that, because a jump INWARD means the label's compound is not on the goto's
 * scope stack at all, so the walk runs off the top and fires `outer` there
 * too. The symptom is a doubled 'E' in the wrong place -- `AEB.E` instead of
 * `AB.E` -- and every other cleanup fixture in the tree passes while it
 * happens, which is why this row exists.
 *
 * The '.' is `mark`, which prints a fixed character rather than the value of
 * the variable it was handed -- that variable was jumped over and never
 * initialized. See the note above. */
static void marko(int *p)
{
    (void)p;
    printf("E");
}

static void jump_in_from_a_cleanup_scope(void)
{
    int outer __attribute__((cleanup(marko))) = 1;

    (void)outer;
    goto inside;
    {
        int a __attribute__((cleanup(dot))) = 2;

        (void)a;
    inside:
        printf("B");
    }
}

int main(void)
{
    printf("A");
    jump_in();
    printf("D|");
    printf("A");
    jump_in_from_a_cleanup_scope();
    printf("\n");
    return 0;
}
