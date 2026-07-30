#include <string.h>

#include "ir/ir.h"
#include "unit.h"
#include "util/arena.h"

/* Dominator tests: Cooper-Harvey-Kennedy vs HAND-COMPUTED idom tables on
 * the three DoD shapes — a diamond, a loop, and an irreducible CFG — plus
 * unreachable-block behavior. CFGs come in as .cgfir text (the parser is
 * the pass-test front door from here on). */

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
} DomFix;

static void dom_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    DomFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        f->errors++;
}

static IrFunc *load_func(TestCtx *t, DomFix *f, const char *src)
{
    DiagSink sink;
    IrModule *m;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = dom_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
    m = ir_parse_module(&f->arena, f->dc, src, "<dom>");
    T_ASSERT(t, m != NULL && m->nfuncs == 1);
    return m ? &m->funcs[0] : NULL;
}

void test_ir_dom_diamond(TestCtx *t)
{
    DomFix f;
    /* entry -> a, b; a -> exit; b -> exit.
     * idom: a=entry, b=entry, exit=entry (join point!). */
    IrFunc *fn = load_func(t, &f,
                           "func void @d(i32 %c) {\n"
                           "entry():\n"
                           "    condbr %c, a(), b()\n"
                           "a():\n"
                           "    br exit()\n"
                           "b():\n"
                           "    br exit()\n"
                           "exit():\n"
                           "    ret\n"
                           "}\n");
    IrDomTree *dt;
    BlockId entry = {1}, a = {2}, b = {3}, exitb = {4};

    if (!fn)
        return;
    dt = ir_domtree_build(&f.arena, fn);
    T_ASSERT_EQ_INT(t, ir_idom(dt, entry).v, 0);
    T_ASSERT_EQ_INT(t, ir_idom(dt, a).v, 1);
    T_ASSERT_EQ_INT(t, ir_idom(dt, b).v, 1);
    T_ASSERT_EQ_INT(t, ir_idom(dt, exitb).v, 1);
    T_ASSERT(t, ir_dominates(dt, entry, exitb));
    T_ASSERT(t, !ir_dominates(dt, a, exitb));
    T_ASSERT(t, !ir_dominates(dt, b, exitb));
    T_ASSERT(t, ir_dominates(dt, a, a)); /* reflexive */
    T_ASSERT(t, !ir_dominates(dt, a, b));
    arena_free_all(&f.arena);
}

void test_ir_dom_loop(TestCtx *t)
{
    DomFix f;
    /* entry -> loop; loop -> body, done; body -> loop (back edge).
     * idom: loop=entry, body=loop, done=loop. The back edge must not
     * confuse the fixpoint. */
    IrFunc *fn = load_func(t, &f,
                           "func void @l(i32 %c) {\n"
                           "entry():\n"
                           "    br loop()\n"
                           "loop():\n"
                           "    condbr %c, body(), done()\n"
                           "body():\n"
                           "    br loop()\n"
                           "done():\n"
                           "    ret\n"
                           "}\n");
    IrDomTree *dt;
    BlockId entry = {1}, loop = {2}, body = {3}, done = {4};

    if (!fn)
        return;
    dt = ir_domtree_build(&f.arena, fn);
    T_ASSERT_EQ_INT(t, ir_idom(dt, loop).v, 1);
    T_ASSERT_EQ_INT(t, ir_idom(dt, body).v, 2);
    T_ASSERT_EQ_INT(t, ir_idom(dt, done).v, 2);
    T_ASSERT(t, ir_dominates(dt, entry, body));
    T_ASSERT(t, ir_dominates(dt, loop, body));
    T_ASSERT(t, ir_dominates(dt, loop, done));
    T_ASSERT(t, !ir_dominates(dt, body, loop)); /* back edge is not dom */
    T_ASSERT(t, !ir_dominates(dt, body, done));
    arena_free_all(&f.arena);
}

void test_ir_dom_irreducible(TestCtx *t)
{
    DomFix f;
    /* The 4-block irreducible shape: entry -> a AND entry -> b, with
     * a <-> b (a cycle with two entries — no natural loop header), both
     * reaching x. NEITHER a nor b dominates the other, so:
     * idom: a=entry, b=entry, x=entry. This is the shape that breaks
     * naive interval/structural analyses; CHK just converges. */
    IrFunc *fn = load_func(t, &f,
                           "func void @ir(i32 %c) {\n"
                           "entry():\n"
                           "    condbr %c, a(), b()\n"
                           "a():\n"
                           "    condbr %c, b(), x()\n"
                           "b():\n"
                           "    condbr %c, a(), x()\n"
                           "x():\n"
                           "    ret\n"
                           "}\n");
    IrDomTree *dt;
    BlockId entry = {1}, a = {2}, b = {3}, x = {4};

    if (!fn)
        return;
    dt = ir_domtree_build(&f.arena, fn);
    T_ASSERT_EQ_INT(t, ir_idom(dt, a).v, 1);
    T_ASSERT_EQ_INT(t, ir_idom(dt, b).v, 1);
    T_ASSERT_EQ_INT(t, ir_idom(dt, x).v, 1);
    T_ASSERT(t, !ir_dominates(dt, a, b));
    T_ASSERT(t, !ir_dominates(dt, b, a));
    T_ASSERT(t, !ir_dominates(dt, a, x));
    T_ASSERT(t, ir_dominates(dt, entry, x));
    arena_free_all(&f.arena);
}

void test_ir_dom_chain_and_unreachable(TestCtx *t)
{
    DomFix f;
    /* Straight-line chain plus an orphan: the orphan gets NO dom entry
     * and dominates/is-dominated by nothing but itself. */
    IrFunc *fn = load_func(t, &f,
                           "func void @c() {\n"
                           "entry():\n"
                           "    br m()\n"
                           "m():\n"
                           "    br e()\n"
                           "e():\n"
                           "    ret\n"
                           "orphan():\n"
                           "    ret\n"
                           "}\n");
    IrDomTree *dt;
    BlockId entry = {1}, mid = {2}, e = {3}, orphan = {4};

    if (!fn)
        return;
    dt = ir_domtree_build(&f.arena, fn);
    T_ASSERT_EQ_INT(t, ir_idom(dt, mid).v, 1);
    T_ASSERT_EQ_INT(t, ir_idom(dt, e).v, 2);
    T_ASSERT(t, ir_dominates(dt, entry, e));
    T_ASSERT(t, ir_dominates(dt, mid, e));
    /* the orphan */
    T_ASSERT_EQ_INT(t, ir_idom(dt, orphan).v, 0);
    T_ASSERT(t, !ir_dominates(dt, entry, orphan));
    T_ASSERT(t, !ir_dominates(dt, orphan, e));
    T_ASSERT(t, ir_dominates(dt, orphan, orphan));
    /* out-of-range ids answer safely */
    T_ASSERT_EQ_INT(t, ir_idom(dt, (BlockId){9}).v, 0);
    T_ASSERT(t, !ir_dominates(dt, (BlockId){0}, e));
    arena_free_all(&f.arena);
}
