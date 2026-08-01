#include "opt/opt.h"
#include "unit.h"

#include <stdio.h>

static void silent_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    (void)user;
    (void)diag;
    (void)dc;
}

static u32 count_op(const IrFunc *f, IrOp op)
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

static void read_report(FILE *report, char *text, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(text, 1, cap - 1, report);
    text[n] = '\0';
}

void test_unroll_trip_count_subword_wrap_exact(TestCtx *t)
{
    u64 trip = UINT64_MAX;

    T_ASSERT(t,
             opt_unroll_trip_count(IRT_I8, ICMP_ULT, 0, 100, 250, true, &trip));
    T_ASSERT_EQ_INT(t, trip, 23);
}

void test_unroll_trip_count_unreachable_ne_target(TestCtx *t)
{
    u64 trip = UINT64_MAX;

    T_ASSERT(t,
             !opt_unroll_trip_count(IRT_I8, ICMP_NE, 0, 2, 255, true, &trip));
    T_ASSERT(t, opt_unroll_trip_count(IRT_I8, ICMP_NE, 0, 2, 254, true, &trip));
    T_ASSERT_EQ_INT(t, trip, 127);
}

void test_unroll_trip_count_nowrap_relations(TestCtx *t)
{
    u64 trip = UINT64_MAX;

    T_ASSERT(t,
             opt_unroll_trip_count(IRT_I32, ICMP_ULT, 3, 4, 20, true, &trip));
    T_ASSERT_EQ_INT(t, trip, 5);
    T_ASSERT(t,
             opt_unroll_trip_count(IRT_I32, ICMP_SLT, 3, 4, 20, false, &trip));
    T_ASSERT_EQ_INT(t, trip, 5);
    T_ASSERT(t, opt_unroll_trip_count(IRT_I32, ICMP_SGT, 20, (u32)-3, 3, false,
                                      &trip));
    T_ASSERT_EQ_INT(t, trip, 6);
}

void test_unroll_trip_count_zero_and_single_iteration(TestCtx *t)
{
    u64 trip = UINT64_MAX;

    T_ASSERT(t, opt_unroll_trip_count(IRT_I16, ICMP_ULT, 9, 1, 4, true, &trip));
    T_ASSERT_EQ_INT(t, trip, 0);
    T_ASSERT(t, opt_unroll_trip_count(IRT_I16, ICMP_EQ, 7, 1, 7, true, &trip));
    T_ASSERT_EQ_INT(t, trip, 1);
    T_ASSERT(t, !opt_unroll_trip_count(IRT_I16, ICMP_EQ, 7, 0, 7, true, &trip));
}

void test_unroll_full_single_latch_loop(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    DiagSink sink = {silent_sink, NULL};
    IrModule *m;
    OptConfig cfg;

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, sink);
    m = ir_parse_module(&arena, dc,
                        "func i32 @sum() {\n"
                        "entry():\n"
                        "    br loop(i32 0, i32 0)\n"
                        "loop(i32 %i, i32 %sum):\n"
                        "    %c = icmp ult i32 %i, 4\n"
                        "    condbr %c, body(), exit(i32 %sum)\n"
                        "body():\n"
                        "    %sum.next = iadd i32 %sum, %i\n"
                        "    %i.next = iadd i32 %i, 1\n"
                        "    br loop(i32 %i.next, i32 %sum.next)\n"
                        "exit(i32 %result):\n"
                        "    ret i32 %result\n"
                        "}\n",
                        "<unroll-full>");
    T_ASSERT(t, m != NULL && ir_verify(dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    cfg.unroll_threshold = 32;
    T_ASSERT(t, m && opt_unroll(m, &cfg));
    if (m) {
        T_ASSERT(t, ir_verify(dc, m));
        T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, 2);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_CONDBR), 0);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_IADD), 8);
        T_ASSERT_EQ_INT(t, m->funcs[0].blocks[0].last->op, IR_BR);
        T_ASSERT_EQ_INT(t, m->funcs[0].blocks[0].last->edges[0].nargs, 1);
    }
    arena_free_all(&arena);
}

void test_unroll_inverted_condition_is_not_full_unrolled(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    DiagSink sink = {silent_sink, NULL};
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char text[512];

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, sink);
    m = ir_parse_module(&arena, dc,
                        "func i32 @sum() {\n"
                        "entry():\n"
                        "    br loop(i32 0, i32 0)\n"
                        "loop(i32 %i, i32 %sum):\n"
                        "    %done = icmp eq i32 %i, 4\n"
                        "    condbr %done, exit(i32 %sum), body()\n"
                        "body():\n"
                        "    %sum.next = iadd i32 %sum, %i\n"
                        "    %i.next = iadd i32 %i, 1\n"
                        "    br loop(i32 %i.next, i32 %sum.next)\n"
                        "exit(i32 %result):\n"
                        "    ret i32 %result\n"
                        "}\n",
                        "<unroll-inverted>");
    T_ASSERT(t, m != NULL && ir_verify(dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    cfg.bail_log = true;
    cfg.report = report;
    if (m)
        (void)opt_unroll(m, &cfg);
    if (report) {
        read_report(report, text, sizeof(text));
        T_ASSERT(t, strstr(text, "unroll_runtime_unsupported") != NULL);
        fclose(report);
    }
    if (m) {
        T_ASSERT(t, ir_verify(dc, m));
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_CONDBR), 1);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_IADD), 2);
    }
    arena_free_all(&arena);
}

void test_unroll_deferred_shapes_are_preserved_with_exact_bails(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    DiagSink sink = {silent_sink, NULL};
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char text[1024];

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, sink);
    m = ir_parse_module(&arena, dc,
                        "func i32 @partial() {\n"
                        "entry():\n"
                        "    br loop(i32 0, i32 0)\n"
                        "loop(i32 %i, i32 %sum):\n"
                        "    %more = icmp ult i32 %i, 9\n"
                        "    condbr %more, body(), exit(i32 %sum)\n"
                        "body():\n"
                        "    %sum.next = iadd i32 %sum, %i\n"
                        "    %i.next = iadd i32 %i, 1\n"
                        "    br loop(i32 %i.next, i32 %sum.next)\n"
                        "exit(i32 %result):\n"
                        "    ret i32 %result\n"
                        "}\n"
                        "func i32 @runtime(i32 %n) {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %more = icmp ult i32 %i, %n\n"
                        "    condbr %more, body(), exit()\n"
                        "body():\n"
                        "    %i.next = iadd i32 %i, 1\n"
                        "    br loop(i32 %i.next)\n"
                        "exit():\n"
                        "    ret i32 %i\n"
                        "}\n"
                        "func void @pinned(ptr %p) {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %more = icmp ult i32 %i, 4\n"
                        "    condbr %more, body(), exit()\n"
                        "body():\n"
                        "    %v = load i32, %p, align 4, volatile, etype i32\n"
                        "    %i.next = iadd i32 %i, 1\n"
                        "    br loop(i32 %i.next)\n"
                        "exit():\n"
                        "    ret\n"
                        "}\n",
                        "<unroll-deferred>");
    T_ASSERT(t, m != NULL && ir_verify(dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    cfg.bail_log = true;
    cfg.report = report;
    if (m)
        (void)opt_unroll(m, &cfg);
    if (report) {
        read_report(report, text, sizeof(text));
        T_ASSERT(t, strstr(text, "unroll_runtime_unsupported") != NULL);
        T_ASSERT(t, strstr(text, "unroll_pinned") != NULL);
        fclose(report);
    }
    if (m) {
        T_ASSERT(t, ir_verify(dc, m));
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_CONDBR), 1);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[1], IR_CONDBR), 1);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[2], IR_CONDBR), 1);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[2], IR_LOAD), 1);
    }
    arena_free_all(&arena);
}

void test_unroll_wrap_and_multi_exit_bails_are_exact(TestCtx *t)
{
    Arena arena;
    DiagCtx *dc;
    DiagSink sink = {silent_sink, NULL};
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char text[1024];

    arena_init(&arena);
    dc = diag_ctx_new(&arena);
    diag_set_sink(dc, sink);
    m = ir_parse_module(&arena, dc,
                        "func i32 @wrap() {\n"
                        "entry():\n"
                        "    br loop(i8 0)\n"
                        "loop(i8 %i):\n"
                        "    %c = icmp ne i8 %i, 255\n"
                        "    condbr %c, body(), exit()\n"
                        "body():\n"
                        "    %next = iadd i8 %i, 2\n"
                        "    br loop(i8 %next)\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n"
                        "func i32 @two_exit(i32 %n) {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %go = icmp ult i32 %i, 9\n"
                        "    condbr %go, body(), done()\n"
                        "body():\n"
                        "    %early = icmp eq i32 %i, %n\n"
                        "    %next = iadd i32 %i, 1\n"
                        "    condbr %early, early(), loop(i32 %next)\n"
                        "done():\n"
                        "    ret i32 0\n"
                        "early():\n"
                        "    ret i32 1\n"
                        "}\n",
                        "<unroll-bails>");
    T_ASSERT(t, m != NULL && ir_verify(dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O3);
    cfg.bail_log = true;
    cfg.report = report;
    if (m)
        (void)opt_unroll(m, &cfg);
    if (report) {
        read_report(report, text, sizeof(text));
        T_ASSERT(t, strstr(text, "unroll_trip_wrap") != NULL);
        T_ASSERT(t, strstr(text, "unroll_multi_exit") != NULL);
        fclose(report);
    }
    if (m) {
        T_ASSERT(t, ir_verify(dc, m));
        T_ASSERT(t, count_op(&m->funcs[0], IR_CONDBR) != 0);
        T_ASSERT(t, count_op(&m->funcs[1], IR_CONDBR) != 0);
    }
    arena_free_all(&arena);
}
