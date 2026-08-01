#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct LoopFix {
    Arena arena;
    DiagCtx *dc;
    int errors;
} LoopFix;

static void loop_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    LoopFix *fix = user;

    (void)dc;
    if (d->level >= DIAG_ERROR)
        fix->errors++;
}

static void loop_fix_init(LoopFix *fix)
{
    DiagSink sink;

    memset(fix, 0, sizeof(*fix));
    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    sink.handle = loop_sink;
    sink.user = fix;
    diag_set_sink(fix->dc, sink);
}

static IrModule *parse_loop(LoopFix *fix, const char *text)
{
    return ir_parse_module(&fix->arena, fix->dc, text, "<loop-tree-test>");
}

static BlockId named_block(const IrFunc *f, const char *name)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++)
        if (f->blocks[bi].name && strcmp(f->blocks[bi].name, name) == 0)
            return (BlockId){bi + 1};
    return BLOCK_INVALID;
}

void test_loop_tree_nested_and_canonical_idempotent(TestCtx *t)
{
    static const char source[] = "func i32 @nested(i32 %n, i32 %m) {\n"
                                 "entry():\n"
                                 "    br outer(i32 0)\n"
                                 "outer(i32 %i):\n"
                                 "    %oc = icmp slt i32 %i, %n\n"
                                 "    condbr %oc, inner(i32 0), exit(i32 %i)\n"
                                 "inner(i32 %j):\n"
                                 "    %ic = icmp slt i32 %j, %m\n"
                                 "    condbr %ic, innerbody(), outerlatch()\n"
                                 "innerbody():\n"
                                 "    %jn = iadd i32 %j, 1\n"
                                 "    br inner(i32 %jn)\n"
                                 "outerlatch():\n"
                                 "    %in = iadd i32 %i, 1\n"
                                 "    br outer(i32 %in)\n"
                                 "exit(i32 %r):\n"
                                 "    ret i32 %r\n"
                                 "}\n";
    LoopFix fix;
    IrModule *m;
    IrFunc *f;
    Arena scratch;
    IrDomTree *dom;
    LoopTree *tree;
    const Loop *inner;
    char why[256];

    loop_fix_init(&fix);
    m = parse_loop(&fix, source);
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    f = &m->funcs[0];
    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    tree = loop_tree_build(&scratch, f, dom);
    T_ASSERT_EQ_INT(t, loop_tree_count(tree), 2);
    inner = loop_tree_innermost(tree, named_block(f, "innerbody"));
    T_ASSERT(t, inner != NULL);
    T_ASSERT_EQ_INT(t, loop_depth(inner), 1);
    T_ASSERT(t, loop_parent(inner) != NULL);
    T_ASSERT(t, loop_canonicalize(m, f, tree));
    arena_free_all(&scratch);

    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    tree = loop_tree_build(&scratch, f, dom);
    T_ASSERT(t, loop_tree_verify_canonical(tree, f, why, sizeof(why)));
    T_ASSERT(t, !loop_canonicalize(m, f, tree));
    T_ASSERT(t, ir_verify(fix.dc, m));
    arena_free_all(&scratch);
    arena_free_all(&fix.arena);
}

void test_loop_tree_lcssa_repairs_multi_exit_join(TestCtx *t)
{
    static const char source[] = "func i32 @liveout(i32 %n, i32 %pick) {\n"
                                 "entry():\n"
                                 "    br loop(i32 0)\n"
                                 "loop(i32 %i):\n"
                                 "    %x = iadd i32 %i, 10\n"
                                 "    %y = imul i32 %i, 3\n"
                                 "    %a = icmp eq i32 %pick, 1\n"
                                 "    condbr %a, exit1(), body()\n"
                                 "body():\n"
                                 "    %b = icmp eq i32 %pick, 2\n"
                                 "    condbr %b, exit2(), latch()\n"
                                 "latch():\n"
                                 "    %next = iadd i32 %i, 1\n"
                                 "    %more = icmp slt i32 %next, %n\n"
                                 "    condbr %more, loop(i32 %next), exit2()\n"
                                 "exit1():\n"
                                 "    br join()\n"
                                 "exit2():\n"
                                 "    br join()\n"
                                 "join():\n"
                                 "    %result = iadd i32 %x, %y\n"
                                 "    ret i32 %result\n"
                                 "}\n";
    LoopFix fix;
    IrModule *m;
    IrFunc *f;
    Arena scratch;
    IrDomTree *dom;
    LoopTree *tree;
    BlockId exit1, exit2, join;
    char why[256];

    loop_fix_init(&fix);
    m = parse_loop(&fix, source);
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    f = &m->funcs[0];
    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    tree = loop_tree_build(&scratch, f, dom);
    T_ASSERT_EQ_INT(t, loop_tree_count(tree), 1);
    T_ASSERT(t, loop_canonicalize(m, f, tree));
    arena_free_all(&scratch);

    exit1 = named_block(f, "exit1");
    exit2 = named_block(f, "exit2");
    join = named_block(f, "join");
    T_ASSERT_EQ_INT(t, ir_block(f, exit1)->nparams, 2);
    T_ASSERT_EQ_INT(t, ir_block(f, exit2)->nparams, 2);
    T_ASSERT_EQ_INT(t, ir_block(f, join)->nparams, 2);
    T_ASSERT(t, ir_verify(fix.dc, m));
    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    tree = loop_tree_build(&scratch, f, dom);
    T_ASSERT(t, loop_tree_verify_canonical(tree, f, why, sizeof(why)));
    T_ASSERT(t, !loop_canonicalize(m, f, tree));
    arena_free_all(&scratch);
    arena_free_all(&fix.arena);
}

void test_loop_tree_marks_goto_weave_irreducible(TestCtx *t)
{
    static const char source[] = "func i32 @irreducible(i32 %c) {\n"
                                 "entry():\n"
                                 "    condbr %c, left(), right()\n"
                                 "left():\n"
                                 "    br join()\n"
                                 "right():\n"
                                 "    br body()\n"
                                 "join():\n"
                                 "    condbr %c, body(), exit()\n"
                                 "body():\n"
                                 "    condbr %c, join(), exit()\n"
                                 "exit():\n"
                                 "    ret i32 0\n"
                                 "}\n";
    LoopFix fix;
    IrModule *m;
    Arena scratch;
    IrDomTree *dom;
    LoopTree *tree;

    loop_fix_init(&fix);
    m = parse_loop(&fix, source);
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    if (m) {
        arena_init(&scratch);
        dom = ir_domtree_build(&scratch, &m->funcs[0]);
        tree = loop_tree_build(&scratch, &m->funcs[0], dom);
        T_ASSERT(t, loop_tree_irreducible(tree));
        T_ASSERT(t, !loop_canonicalize(m, &m->funcs[0], tree));
        arena_free_all(&scratch);
    }
    arena_free_all(&fix.arena);
}
