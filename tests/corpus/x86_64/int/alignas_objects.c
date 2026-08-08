// OPT_EQ: all
// `_Alignas` on an OBJECT, executed. This is ISO C11 6.7.5, not a GNU
// extension, and it did nothing at all: the alignment was parsed, validated by
// check_alignas, and the return value discarded at the call site, so every
// object path -- file-scope global, function-local static, automatic slot --
// took the TYPE's alignment and dropped the declaration's.
//
// `_Alignas(64) int g;` therefore emitted `.p2align 2` and `&g` was not
// 64-aligned at run time, with no diagnostic anywhere. Members had worked
// since Sprint 14 through Member.align_override, which is what made the hole
// invisible: the feature demonstrably worked, just not on objects.
//
// Checked by ADDRESS rather than by _Alignof: _Alignof answers from the type
// and was correct throughout, so it cannot tell a placed object from a
// misplaced one. Only the runtime address can.
//
// The automatic case stops at 16 -- the frame base is 16-aligned, so a
// stricter request needs a realigned frame and is refused by name (Sprint 53).
// tests/programs/gnu/alignas_overaligned_local.c pins that refusal.

_Alignas(64) int g_big = 1;
_Alignas(32) char g_arr[3] = {1, 2, 3};
_Alignas(16) static long g_stat = 5;
/* No _Alignas: natural alignment must be untouched by all of the above. */
int g_plain = 7;
char g_char = 9;

/* Both declarations carry it. 6.7.5p7 REQUIRES that: an alignment on a
 * declaration but not on the definition is a constraint violation, which gcc
 * accepts and clang rejects -- so spelling it once here would pin a program
 * that is not portable C, not a layout fact. */
extern _Alignas(128) int g_merged;
_Alignas(128) int g_merged = 11;

static int local_static_aligned(void)
{
    _Alignas(32) static long s = 3;

    return ((unsigned long)&s & 31u) == 0;
}

int main(void)
{
    _Alignas(16) int loc = 4;
    _Alignas(8) char cloc = 6;

    if (((unsigned long)&g_big & 63u) != 0)
        return 1;
    if (((unsigned long)g_arr & 31u) != 0)
        return 2;
    if (((unsigned long)&g_stat & 15u) != 0)
        return 3;
    if (((unsigned long)&g_merged & 127u) != 0)
        return 4;
    if (!local_static_aligned())
        return 5;
    if (((unsigned long)&loc & 15u) != 0)
        return 6;
    if (((unsigned long)&cloc & 7u) != 0)
        return 7;

    /* Natural alignment is unchanged, and the values still round-trip. */
    if (((unsigned long)&g_plain & 3u) != 0)
        return 8;
    if (g_big != 1 || g_arr[2] != 3 || g_stat != 5 || g_plain != 7)
        return 9;
    if (g_char != 9 || g_merged != 11 || loc != 4 || cloc != 6)
        return 10;
    return 0;
}
