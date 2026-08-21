#include "opt/opt.h"
#include "unit.h"

#include <stdio.h>
#include <string.h>

static void s35_silent_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    (void)user;
    (void)diag;
    (void)dc;
}

static u32 s35_count_op(const IrFunc *f, IrOp op)
{
    u32 bi;
    u32 count = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == op)
                count++;
    }
    return count;
}

static const IrBlock *s35_find_block(const IrFunc *f, const char *name)
{
    u32 i;

    for (i = 0; i < f->nblocks; i++)
        if (strcmp(f->blocks[i].name, name) == 0)
            return &f->blocks[i];
    return NULL;
}

static void s35_assert_permuted_latch_chain(TestCtx *t, const IrBlock *block,
                                            u32 copies, bool starts_with_params)
{
    const IrInst *in;
    const IrInst *previous_sub = NULL;
    const IrInst *previous_increment = NULL;
    u32 seen = 0;

    T_ASSERT(t, block != NULL);
    if (!block)
        return;
    for (in = block->first; in; in = in->next) {
        if (in->op == IR_ISUB) {
            T_ASSERT_EQ_INT(t, in->nops, 2);
            if (seen == 0 && starts_with_params) {
                T_ASSERT_EQ_INT(t, in->ops[0].kind, IROP_VALUE);
                T_ASSERT_EQ_INT(t, in->ops[0].a, block->params[1].v);
                T_ASSERT_EQ_INT(t, in->ops[1].kind, IROP_VALUE);
                T_ASSERT_EQ_INT(t, in->ops[1].a, block->params[0].v);
            } else if (seen == 0) {
                T_ASSERT_EQ_INT(t, in->ops[0].kind, IROP_ICONST);
                T_ASSERT_EQ_INT(t, in->ops[0].a, 0);
                T_ASSERT_EQ_INT(t, in->ops[1].kind, IROP_ICONST);
                T_ASSERT_EQ_INT(t, in->ops[1].a, 10);
            } else {
                T_ASSERT(t, previous_sub != NULL);
                T_ASSERT(t, previous_increment != NULL);
                T_ASSERT_EQ_INT(t, in->ops[0].kind, IROP_VALUE);
                T_ASSERT_EQ_INT(t, in->ops[0].a, previous_increment->result.v);
                T_ASSERT_EQ_INT(t, in->ops[1].kind, IROP_VALUE);
                T_ASSERT_EQ_INT(t, in->ops[1].a, previous_sub->result.v);
            }
            previous_sub = in;
            seen++;
        } else if (in->op == IR_IADD) {
            previous_increment = in;
        }
    }
    T_ASSERT_EQ_INT(t, seen, copies);
}

static IrModule *partial_module(Arena *arena, DiagCtx *dc, u32 trip)
{
    char source[1024];

    snprintf(source, sizeof(source),
             "func i32 @sum() {\n"
             "entry():\n"
             "    br loop(i32 0, i32 0)\n"
             "loop(i32 %%i, i32 %%sum):\n"
             "    %%c = icmp ult i32 %%i, %u\n"
             "    condbr %%c, body(), exit(i32 %%sum)\n"
             "body():\n"
             "    %%sum.next = iadd i32 %%sum, %%i\n"
             "    %%i.next = iadd i32 %%i, 1\n"
             "    br loop(i32 %%i.next, i32 %%sum.next)\n"
             "exit(i32 %%result):\n"
             "    ret i32 %%result\n"
             "}\n",
             trip);
    return ir_parse_module(arena, dc, source, "<unroll-partial-s35>");
}

void test_unroll_s35_constant_factor_four_shapes(TestCtx *t)
{
    u32 trip;

    for (trip = 9; trip <= 12; trip++) {
        Arena arena;
        DiagCtx *dc;
        DiagSink sink = {s35_silent_sink, NULL};
        IrModule *m;
        OptConfig cfg;
        u32 remainder = trip % 4;

        arena_init(&arena);
        dc = diag_ctx_new(&arena);
        diag_set_sink(dc, sink);
        m = partial_module(&arena, dc, trip);
        T_ASSERT(t, m != NULL && ir_verify(dc, m));
        opt_config_init(&cfg, OPT_O3);
        cfg.verify_after_each = true;
        T_ASSERT(t, m && opt_unroll(m, &cfg));
        if (m) {
            T_ASSERT(t, ir_verify(dc, m));
            T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_CONDBR), 1);
            /* Four serial operations in the grouped body, plus the peeled
             * constant remainder in the preheader. */
            T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_IADD),
                            8 + 2 * remainder);
        }
        arena_free_all(&arena);
    }
}

void test_unroll_s35_partial_remaps_latch_parameters(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    DiagSink sink = {s35_silent_sink, NULL};
    IrModule *m;
    OptConfig cfg;

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, sink);
    m = ir_parse_module(
        &arena, dc,
        "func i32 @sum() {\n"
        "entry():\n"
        "    br loop(i32 0, i32 10)\n"
        "loop(i32 %i, i32 %sum):\n"
        "    %c = icmp ult i32 %i, 9\n"
        "    condbr %c, body(i32 %sum, i32 %i), exit(i32 %sum)\n"
        "body(i32 %rhs, i32 %lhs):\n"
        "    %sum.next = isub i32 %lhs, %rhs\n"
        "    %i.next = iadd i32 %i, 1\n"
        "    br loop(i32 %i.next, i32 %sum.next)\n"
        "exit(i32 %result):\n"
        "    ret i32 %result\n"
        "}\n",
        "<unroll-partial-latch-params-s35>");
    T_ASSERT(t, m != NULL && ir_verify(dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_unroll(m, &cfg));
    if (m) {
        T_ASSERT(t, ir_verify(dc, m));
        T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_CONDBR), 1);
        T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_IADD), 5);
        T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_ISUB), 5);
        s35_assert_permuted_latch_chain(
            t, s35_find_block(&m->funcs[0], "entry"), 1, false);
        s35_assert_permuted_latch_chain(t, s35_find_block(&m->funcs[0], "body"),
                                        4, true);
    }
    arena_free_all(&arena);
}

void test_unroll_s35_pinned_partial_pass_manager_audit(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    DiagSink sink = {s35_silent_sink, NULL};
    IrModule *m;
    OptConfig cfg;
    const Pass *passes[] = {&OPT_PASS_UNROLL};

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, sink);
    m = ir_parse_module(&arena, dc,
                        "func i32 @pinned(ptr %p) {\n"
                        "entry():\n"
                        "    br loop(i32 0, i32 0)\n"
                        "loop(i32 %i, i32 %sum):\n"
                        "    %c = icmp ult i32 %i, 9\n"
                        "    condbr %c, body(), exit(i32 %sum)\n"
                        "body():\n"
                        "    %v = load i32, %p, align 4, volatile, etype i32\n"
                        "    %sum.next = iadd i32 %sum, %v\n"
                        "    %i.next = iadd i32 %i, 1\n"
                        "    br loop(i32 %i.next, i32 %sum.next)\n"
                        "exit(i32 %result):\n"
                        "    ret i32 %result\n"
                        "}\n",
                        "<unroll-pinned-s35>");
    T_ASSERT(t, m != NULL && ir_verify(dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_run_pass_sequence(m, &cfg, passes, 1));
    if (m) {
        T_ASSERT(t, ir_verify(dc, m));
        T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_LOAD), 5);
        T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_CONDBR), 1);
    }
    arena_free_all(&arena);
}

void test_unroll_s35_peel_remaps_header_condition_use(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    DiagSink sink = {s35_silent_sink, NULL};
    IrModule *m;
    OptConfig cfg;

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, sink);
    m = ir_parse_module(&arena, dc,
                        "func i32 @condition_use() {\n"
                        "entry():\n"
                        "    br loop(i32 0, i32 0)\n"
                        "loop(i32 %i, i32 %sum):\n"
                        "    %c = icmp ult i32 %i, 9\n"
                        "    condbr %c, body(), exit(i32 %sum)\n"
                        "body():\n"
                        "    %sum.next = iadd i32 %sum, %c\n"
                        "    %i.next = iadd i32 %i, 1\n"
                        "    br loop(i32 %i.next, i32 %sum.next)\n"
                        "exit(i32 %result):\n"
                        "    ret i32 %result\n"
                        "}\n",
                        "<unroll-condition-use-s35>");
    T_ASSERT(t, m != NULL && ir_verify(dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_unroll(m, &cfg));
    if (m) {
        T_ASSERT(t, ir_verify(dc, m));
        T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_CONDBR), 1);
    }
    arena_free_all(&arena);
}

void test_unroll_s35_runtime_bound_remains_explicitly_unsupported(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    DiagSink sink = {s35_silent_sink, NULL};
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char text[512];
    size_t n;

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, sink);
    m = ir_parse_module(&arena, dc,
                        "func i32 @runtime(i32 %n) {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %c = icmp ult i32 %i, %n\n"
                        "    condbr %c, body(), exit()\n"
                        "body():\n"
                        "    %next = iadd i32 %i, 1\n"
                        "    br loop(i32 %next)\n"
                        "exit():\n"
                        "    ret i32 %i\n"
                        "}\n",
                        "<unroll-runtime-s35>");
    T_ASSERT(t, m != NULL && ir_verify(dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O3);
    cfg.bail_log = true;
    cfg.report = report;
    if (m)
        (void)opt_unroll(m, &cfg); /* canonicalization itself may change IR */
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(text, 1, sizeof(text) - 1, report);
        text[n] = '\0';
        T_ASSERT(t, strstr(text, "unroll_runtime_unsupported") != NULL);
        fclose(report);
    }
    if (m) {
        T_ASSERT(t, ir_verify(dc, m));
        T_ASSERT_EQ_INT(t, s35_count_op(&m->funcs[0], IR_CONDBR), 1);
    }
    arena_free_all(&arena);
}
