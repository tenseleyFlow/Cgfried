// OPT_EQ: all
// `aligned` on OBJECTS and on a FUNCTION, executed. In the shared corpus so
// the arm64 lane re-runs it: the bare form and the function position are both
// target-dependent claims, and only running on each target settles them.
//
// Checked by ADDRESS rather than by _Alignof, for the reason the _Alignas work
// established: _Alignof answers from the TYPE and is correct even when the
// object is placed wrong. It cannot tell a placed object from a misplaced one.
//
// The automatic position also exceeds the ABI stack alignment, so the frame
// lowering must align the object rather than merely its offset.
#define A(n) __attribute__((aligned(n)))

A(64) int g_pre = 1; /* prefix binds to the declaration */
int g_suf A(64) = 2; /* suffix binds to its own declarator */
static char g_arr[3] A(32);
int g_expr A(4 * 8) = 3; /* a constant EXPRESSION, not a literal */
/* No argument: the target's biggest alignment, 16 on both Linux targets. */
int g_bare __attribute__((aligned)) = 4;
/* Weaker than natural: silently declined, never an error. The object keeps
 * its natural 4 and the program still runs. */
int g_weak A(1) = 5;

static void aligned_fn(void) A(64);
static void aligned_fn(void)
{
}

static int local_static(void)
{
    static long s A(32) = 6;

    return ((unsigned long)&s & 31u) == 0;
}

int main(void)
{
    A(64) int loc = 7;

    if (((unsigned long)&g_pre & 63u) != 0)
        return 1;
    if (((unsigned long)&g_suf & 63u) != 0)
        return 2;
    if (((unsigned long)g_arr & 31u) != 0)
        return 3;
    if (((unsigned long)&g_expr & 31u) != 0)
        return 4;
    if (((unsigned long)&g_bare & 15u) != 0)
        return 5;
    if (((unsigned long)&g_weak & 3u) != 0)
        return 6;
    if (!local_static())
        return 7;
    if (((unsigned long)&loc & 63u) != 0)
        return 8;
    /* The FUNCTION position aligns the CODE. */
    if (((unsigned long)(void *)aligned_fn & 63u) != 0)
        return 9;

    aligned_fn();
    if (g_pre != 1 || g_suf != 2 || g_expr != 3)
        return 10;
    if (g_bare != 4 || g_weak != 5 || loc != 7)
        return 11;
    return 0;
}
