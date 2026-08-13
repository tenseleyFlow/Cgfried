#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
} CloneFix;

static void clone_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    CloneFix *fix = user;

    (void)dc;
    if (diag->level >= DIAG_ERROR)
        fix->errors++;
}

static void clone_fix_init(CloneFix *fix)
{
    DiagSink sink;

    memset(fix, 0, sizeof(*fix));
    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    sink.handle = clone_sink;
    sink.user = fix;
    diag_set_sink(fix->dc, sink);
}

static BlockId named_block(const IrFunc *f, const char *name)
{
    u32 i;

    for (i = 0; i < f->nblocks; i++)
        if (f->blocks[i].name && strcmp(f->blocks[i].name, name) == 0)
            return (BlockId){i + 1};
    return BLOCK_INVALID;
}

static const IrInst *find_op(const IrBlock *block, IrOp op)
{
    const IrInst *in;

    for (in = block->first; in; in = in->next)
        if (in->op == op)
            return in;
    return NULL;
}

static const Loop *only_loop(Arena *scratch, IrFunc *f, LoopTree **tree)
{
    IrDomTree *dom = ir_domtree_build(scratch, f);

    *tree = loop_tree_build(scratch, f, dom);
    return loop_tree_count(*tree) == 1 ? loop_tree_at(*tree, 0) : NULL;
}

void test_loop_trip_analyze_constant_runtime_and_inverted(TestCtx *t)
{
    static const char source[] = "func i32 @constant() {\n"
                                 "entry():\n"
                                 "    br loop(i32 0)\n"
                                 "loop(i32 %i):\n"
                                 "    %done = icmp eq i32 %i, 4\n"
                                 "    condbr %done, exit(), body()\n"
                                 "body():\n"
                                 "    %next = iadd nsw i32 %i, 1\n"
                                 "    br loop(i32 %next)\n"
                                 "exit():\n"
                                 "    ret i32 %i\n"
                                 "}\n"
                                 "func i32 @runtime(i32 %n) {\n"
                                 "entry():\n"
                                 "    br loop(i32 0)\n"
                                 "loop(i32 %i):\n"
                                 "    %more = icmp ult i32 %i, %n\n"
                                 "    condbr %more, body(), exit()\n"
                                 "body():\n"
                                 "    %next = iadd i32 %i, 1\n"
                                 "    br loop(i32 %next)\n"
                                 "exit():\n"
                                 "    ret i32 %i\n"
                                 "}\n"
                                 "func i32 @commuted() {\n"
                                 "entry():\n"
                                 "    br loop(i32 0)\n"
                                 "loop(i32 %i):\n"
                                 "    %more = icmp slt i32 %i, 5\n"
                                 "    condbr %more, body(), exit()\n"
                                 "body():\n"
                                 "    %next = iadd nsw i32 1, %i\n"
                                 "    br loop(i32 %next)\n"
                                 "exit():\n"
                                 "    ret i32 %i\n"
                                 "}\n";
    CloneFix fix;
    IrModule *m;
    u32 fi;

    clone_fix_init(&fix);
    m = ir_parse_module(&fix.arena, fix.dc, source, "<trip-analysis>");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    for (fi = 0; fi < m->nfuncs; fi++) {
        Arena scratch;
        LoopTree *tree;
        const Loop *loop;
        TripInfo trip;
        const char *reason = "not-cleared";

        arena_init(&scratch);
        loop = only_loop(&scratch, &m->funcs[fi], &tree);
        T_ASSERT(t, loop != NULL);
        T_ASSERT(t, loop && loop_trip_analyze(&m->funcs[fi], loop, false, &trip,
                                              &reason));
        T_ASSERT(t, reason == NULL);
        if (fi == 0) {
            T_ASSERT_EQ_INT(t, trip.kind, LOOP_TRIP_CONSTANT);
            T_ASSERT_EQ_INT(t, trip.constant, 4);
            T_ASSERT_EQ_INT(t, trip.induction.pred, ICMP_NE);
            T_ASSERT_EQ_INT(t, trip.induction.continue_edge, 1);
            T_ASSERT(t, trip.induction.signed_no_wrap);
            T_ASSERT(t, trip.induction.modular);
        } else if (fi == 1) {
            T_ASSERT_EQ_INT(t, trip.kind, LOOP_TRIP_RUNTIME);
            T_ASSERT_EQ_INT(t, trip.induction.pred, ICMP_ULT);
            T_ASSERT(t, trip.induction.modular);
            T_ASSERT(t, loop_operand_invariant(&m->funcs[fi], loop,
                                               trip.induction.bound));
            T_ASSERT(t, !loop_operand_invariant(&m->funcs[fi], loop,
                                                ir_op_undef(IRT_I32)));
        } else {
            T_ASSERT_EQ_INT(t, trip.kind, LOOP_TRIP_CONSTANT);
            T_ASSERT_EQ_INT(t, trip.constant, 5);
            T_ASSERT_EQ_INT(t, trip.induction.step.a, 1);
            T_ASSERT(t, !trip.induction.subtract_step);
        }
        arena_free_all(&scratch);
    }
    arena_free_all(&fix.arena);
}

void test_loop_clone_region_growth_lcssa_flags_and_locations(TestCtx *t)
{
    static const char source[] =
        "func i32 @clone(ptr %p, i32 %n) {\n"
        "entry():\n"
        "    br pad1()\n"
        "pad1():\n"
        "    br pad2()\n"
        "pad2():\n"
        "    br header(i32 0, i32 0)\n"
        "header(i32 %i, i32 %sum):\n"
        "    %more = icmp ult i32 %i, %n\n"
        "    %x = iadd nsw i32 %sum, 10\n"
        "    %y = imul nsw i32 %i, 3\n"
        "    condbr %more, body(), exit(i32 %x, i32 %y)\n"
        "body():\n"
        "    %v = load i32, %p, align 4, volatile, etype i32\n"
        "    %a = iadd i32 %x, %v\n"
        "    %b = iadd i32 %a, 1\n"
        "    %c = iadd i32 %b, 2\n"
        "    %ignored = call i32 @sink(ptr %p, i32 %n anon, i32 %c anon) va\n"
        "    %d = iadd i32 %c, 3\n"
        "    %e = iadd i32 %d, 4\n"
        "    %f = iadd i32 %e, 5\n"
        "    br latch(i32 %f)\n"
        "latch(i32 %nextsum):\n"
        "    %next = iadd nsw i32 %i, 1\n"
        "    br header(i32 %next, i32 %nextsum)\n"
        "exit(i32 %outx, i32 %outy):\n"
        "    %result = iadd i32 %outx, %outy\n"
        "    br done(i32 %result)\n"
        "done(i32 %final):\n"
        "    ret i32 %final\n"
        "}\n";
    CloneFix fix;
    IrModule *m;
    IrFunc *f;
    BlockId region[3], clone_header, clone_body, clone_latch, exit;
    LoopCloneMap map;
    const char *reason = NULL;
    const IrInst *old_load, *new_load, *old_add, *new_add, *new_call;
    const IrEdge *exit_edge;
    Span span;
    u32 old_blocks, old_values, old_block_cap, old_value_cap;

    clone_fix_init(&fix);
    m = ir_parse_module(&fix.arena, fix.dc, source, "<loop-clone>");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    f = &m->funcs[0];
    region[0] = named_block(f, "header");
    region[1] = named_block(f, "body");
    region[2] = named_block(f, "latch");
    exit = named_block(f, "exit");
    old_blocks = f->nblocks;
    old_values = f->nvals;
    old_block_cap = f->cap_blocks;
    old_value_cap = f->cap_vals;
    memset(&span, 0, sizeof(span));
    span.file_id = 9;
    span.line = 37;
    span.col = 5;
    span.len = 4;
    ((IrInst *)find_op(ir_block(f, region[1]), IR_LOAD))->loc =
        ir_intern_span(m, span);

    T_ASSERT(t, !loop_clone_region(m, f, region, 3, region[0],
                                   LOOP_CLONE_REJECT_PINNED, "copy", &map,
                                   &reason));
    T_ASSERT_EQ_STR(t, reason, "clone_pinned");
    T_ASSERT_EQ_INT(t, f->nblocks, old_blocks);
    T_ASSERT_EQ_INT(t, f->nvals, old_values);

    reason = "not-cleared";
    T_ASSERT(t, loop_clone_region(m, f, region, 3, region[0],
                                  LOOP_CLONE_PATH_EXCLUSIVE, "copy", &map,
                                  &reason));
    T_ASSERT(t, reason == NULL);
    T_ASSERT(t, map.cloned_pinned);
    T_ASSERT_EQ_INT(t, f->nblocks, old_blocks + 3);
    T_ASSERT(t, f->cap_blocks > old_block_cap);
    T_ASSERT(t, f->cap_vals > old_value_cap);
    clone_header = loop_clone_block(&map, region[0]);
    clone_body = loop_clone_block(&map, region[1]);
    clone_latch = loop_clone_block(&map, region[2]);
    T_ASSERT_EQ_INT(t, map.entry.v, clone_header.v);
    T_ASSERT_EQ_INT(t, ir_block(f, clone_header)->last->edges[0].target.v,
                    clone_body.v);
    T_ASSERT_EQ_INT(t, ir_block(f, clone_latch)->last->edges[0].target.v,
                    clone_header.v);

    /* The exit stays shared, while both LCSSA arguments are cloned values. */
    exit_edge = &ir_block(f, clone_header)->last->edges[1];
    T_ASSERT_EQ_INT(t, exit_edge->target.v, exit.v);
    T_ASSERT_EQ_INT(t, exit_edge->nargs, 2);
    T_ASSERT_EQ_INT(
        t, exit_edge->args[0].a,
        loop_clone_operand(&map, ir_block(f, region[0])->last->edges[1].args[0])
            .a);
    T_ASSERT_EQ_INT(
        t, exit_edge->args[1].a,
        loop_clone_operand(&map, ir_block(f, region[0])->last->edges[1].args[1])
            .a);
    T_ASSERT(t, exit_edge->args[0].a !=
                    ir_block(f, region[0])->last->edges[1].args[0].a);

    old_load = find_op(ir_block(f, region[1]), IR_LOAD);
    new_load = find_op(ir_block(f, clone_body), IR_LOAD);
    T_ASSERT(t, old_load != NULL && new_load != NULL);
    T_ASSERT_EQ_INT(t, new_load->flags, old_load->flags);
    T_ASSERT_EQ_INT(t, new_load->loc, old_load->loc);
    T_ASSERT_EQ_INT(t, new_load->align, old_load->align);
    new_call = find_op(ir_block(f, clone_body), IR_CALL);
    T_ASSERT(t, new_call != NULL);
    if (new_call) {
        T_ASSERT_EQ_INT(t, new_call->nops, 3);
        T_ASSERT_EQ_INT(t, new_call->ops[1].argflags, IROPF_ANON);
        T_ASSERT_EQ_INT(t, new_call->ops[2].argflags, IROPF_ANON);
    }
    old_add = find_op(ir_block(f, region[0]), IR_IADD);
    new_add = find_op(ir_block(f, clone_header), IR_IADD);
    T_ASSERT(t, old_add != NULL && new_add != NULL);
    T_ASSERT_EQ_INT(t, new_add->flags, old_add->flags);
    T_ASSERT_EQ_INT(
        t, new_add->result.v,
        loop_clone_operand(&map, ir_op_value(f, old_add->result)).a);
    arena_free_all(&fix.arena);
}

void test_loop_clone_region_rejects_duplicate_and_outside_entry(TestCtx *t)
{
    static const char source[] = "func void @bad() {\n"
                                 "entry():\n"
                                 "    br a()\n"
                                 "a():\n"
                                 "    br b()\n"
                                 "b():\n"
                                 "    ret\n"
                                 "}\n";
    CloneFix fix;
    IrModule *m;
    IrFunc *f;
    BlockId duplicate[2], one[1];
    LoopCloneMap map;
    const char *reason = NULL;
    u32 old_blocks;

    clone_fix_init(&fix);
    m = ir_parse_module(&fix.arena, fix.dc, source, "<bad-loop-clone>");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    f = &m->funcs[0];
    duplicate[0] = named_block(f, "a");
    duplicate[1] = duplicate[0];
    one[0] = duplicate[0];
    old_blocks = f->nblocks;
    T_ASSERT(t,
             !loop_clone_region(m, f, duplicate, 2, duplicate[0],
                                LOOP_CLONE_REJECT_PINNED, NULL, &map, &reason));
    T_ASSERT_EQ_STR(t, reason, "clone_duplicate_block");
    T_ASSERT(t,
             !loop_clone_region(m, f, one, 1, named_block(f, "b"),
                                LOOP_CLONE_REJECT_PINNED, NULL, &map, &reason));
    T_ASSERT_EQ_STR(t, reason, "clone_entry_outside");
    T_ASSERT_EQ_INT(t, f->nblocks, old_blocks);
    arena_free_all(&fix.arena);
}
