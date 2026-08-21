#include "pp/pp.h"
#include "unit.h"

void test_pp_loc_file_roundtrip(TestCtx *t)
{
    LocTable lt;
    SrcLoc a, b;
    FileId f;
    u32 line, col;

    pp_loc_init(&lt);
    a = pp_loc_file(&lt, 1, 10, 5);
    b = pp_loc_file(&lt, 2, 99, 1);
    T_ASSERT(t, a != SRCLOC_INVALID && b != SRCLOC_INVALID && a != b);
    T_ASSERT(t, !pp_loc_is_expansion(&lt, a));
    pp_loc_resolve(&lt, a, &f, &line, &col);
    T_ASSERT_EQ_INT(t, f, 1);
    T_ASSERT_EQ_INT(t, line, 10);
    T_ASSERT_EQ_INT(t, col, 5);
    pp_loc_resolve(&lt, b, &f, &line, &col);
    T_ASSERT_EQ_INT(t, f, 2);
    T_ASSERT(t, pp_loc_expansion_parent(&lt, a) == SRCLOC_INVALID);
    pp_loc_free(&lt);
}

void test_pp_loc_expansion_chain(TestCtx *t)
{
    LocTable lt;
    SrcLoc spell, inv, e1, e2, e3;
    FileId f;
    u32 line, col;
    int i;

    pp_loc_init(&lt);
    spell = pp_loc_file(&lt, 1, 2, 3); /* macro body spelling */
    inv = pp_loc_file(&lt, 1, 20, 1);  /* invocation site */
    /* Sprint 7 grew the entry: each frame remembers WHICH macro it
     * expanded, as name + def-loc (not a MacroDef*) so a late diagnostic
     * survives a post-expansion #undef or redefinition. */
    e1 = pp_loc_expansion(&lt, spell, inv, "M1", spell);
    e2 = pp_loc_expansion(&lt, e1, inv, "M2", spell);
    e3 = pp_loc_expansion(&lt, e2, e1, "M3", spell); /* 3-deep chain */

    T_ASSERT(t, pp_loc_is_expansion(&lt, e3));
    /* Resolution walks spelled_at links to the physical spelling. */
    pp_loc_resolve(&lt, e3, &f, &line, &col);
    T_ASSERT_EQ_INT(t, f, 1);
    T_ASSERT_EQ_INT(t, line, 2);
    T_ASSERT_EQ_INT(t, col, 3);
    /* Parent hops one invocation level. */
    T_ASSERT(t, pp_loc_expansion_parent(&lt, e3) == e1);
    T_ASSERT(t, pp_loc_expansion_parent(&lt, e2) == inv);
    T_ASSERT(t, pp_loc_expansion_parent(&lt, e1) == inv);

    /* Frame identity survives table growth and is NULL for file locs. */
    T_ASSERT_EQ_STR(t, pp_loc_macro_name(&lt, e3), "M3");
    T_ASSERT_EQ_STR(t, pp_loc_macro_name(&lt, e1), "M1");
    T_ASSERT(t, pp_loc_macro_name(&lt, spell) == NULL);
    T_ASSERT(t, pp_loc_macro_def(&lt, e2) == spell);
    T_ASSERT(t, pp_loc_macro_def(&lt, inv) == SRCLOC_INVALID);

    /* Growth must not invalidate resolution (ids, never pointers). */
    for (i = 0; i < 5000; i++)
        pp_loc_file(&lt, 3, (u32)i + 1, 1);
    pp_loc_resolve(&lt, e3, &f, &line, &col);
    T_ASSERT_EQ_INT(t, line, 2);
    pp_loc_free(&lt);
}

void test_pp_loc_graft_expansion_chain(TestCtx *t)
{
    LocTable lt;
    SrcLoc spell1, old_use, call, inner, grafted;
    FileId f;
    u32 line, col;

    pp_loc_init(&lt);
    spell1 = pp_loc_file(&lt, 1, 2, 3);
    old_use = pp_loc_file(&lt, 1, 20, 5);
    call = pp_loc_file(&lt, 1, 30, 1);
    inner = pp_loc_expansion(&lt, spell1, old_use, "INNER", spell1);

    grafted = pp_loc_graft_expansions(&lt, inner, call, "OUTER", spell1);
    T_ASSERT_EQ_STR(t, pp_loc_macro_name(&lt, grafted), "INNER");
    grafted = pp_loc_expansion_parent(&lt, grafted);
    T_ASSERT_EQ_STR(t, pp_loc_macro_name(&lt, grafted), "OUTER");
    /* INNER's note anchor is the old argument use, not its definition. */
    pp_loc_resolve(&lt, grafted, &f, &line, &col);
    T_ASSERT_EQ_INT(t, line, 20);
    T_ASSERT_EQ_INT(t, col, 5);
    T_ASSERT(t, pp_loc_expansion_parent(&lt, grafted) == call);
    pp_loc_free(&lt);
}
