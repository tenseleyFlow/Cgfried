#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct BceFix {
    Arena arena;
    DiagCtx *dc;
} BceFix;

static void bce_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void bce_fix_init(BceFix *fix)
{
    DiagSink sink = {bce_sink, NULL};

    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    diag_set_sink(fix->dc, sink);
}

static IrModule *bce_parse(BceFix *fix, const char *source)
{
    return ir_parse_module(&fix->arena, fix->dc, source, "<bce-test>");
}

static IrBlock *bce_block(IrFunc *f, const char *name)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++)
        if (f->blocks[bi].name && strcmp(f->blocks[bi].name, name) == 0)
            return &f->blocks[bi];
    return NULL;
}

static u32 bce_count_op(const IrFunc *f, IrOp op)
{
    u32 bi, count = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == op)
                count++;
    }
    return count;
}

static void bce_read_report(FILE *report, char *out, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(out, 1, cap - 1, report);
    out[n] = '\0';
}

static bool bce_run(IrModule *m, OptConfig *cfg, bool fwrapv, FILE *report)
{
    opt_config_init(cfg, OPT_O2);
    cfg->fwrapv = fwrapv;
    cfg->verify_after_each = true;
    cfg->bail_log = report != NULL;
    cfg->report = report;
    return opt_bce(m, cfg);
}

void test_opt_bce_folds_marked_check_from_dominating_loop_edge(TestCtx *t)
{
    BceFix fix;
    IrModule *m;
    IrBlock *body;
    OptConfig cfg;

    bce_fix_init(&fix);
    m = bce_parse(&fix, "func i32 @f(i32 %n) {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %more = icmp ult i32 %i, %n\n"
                        "    condbr %more, body(), exit()\n"
                        "body():\n"
                        "    %safe = icmp ule i32 %i, %n, bounds\n"
                        "    condbr %safe, latch(), trap()\n"
                        "latch():\n"
                        "    %next = iadd i32 %i, 1\n"
                        "    br loop(i32 %next)\n"
                        "trap():\n"
                        "    ret i32 99\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    T_ASSERT(t, m && bce_run(m, &cfg, false, NULL));
    if (m) {
        body = bce_block(&m->funcs[0], "body");
        T_ASSERT(t, body != NULL && body->last != NULL);
        if (body && body->last) {
            T_ASSERT_EQ_INT(t, body->last->op, IR_CONDBR);
            T_ASSERT_EQ_INT(t, body->last->ops[0].kind, IROP_ICONST);
            T_ASSERT_EQ_INT(t, body->last->ops[0].a, 1);
        }
        T_ASSERT_EQ_INT(t, bce_count_op(&m->funcs[0], IR_ICMP), 1);
        T_ASSERT(t, ir_verify(fix.dc, m));
    }
    arena_free_all(&fix.arena);
}

void test_opt_bce_exact_next_range_folds_divisible_step(TestCtx *t)
{
    BceFix fix;
    IrModule *m;
    IrBlock *body;
    OptConfig cfg;

    bce_fix_init(&fix);
    m = bce_parse(&fix, "func i32 @f() {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %more = icmp ult i32 %i, 12\n"
                        "    condbr %more, body(), exit()\n"
                        "body():\n"
                        "    %next = iadd i32 %i, 3\n"
                        "    %safe = icmp ule i32 %next, 12, bounds\n"
                        "    condbr %safe, loop(i32 %next), trap()\n"
                        "trap():\n"
                        "    ret i32 99\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    T_ASSERT(t, m && bce_run(m, &cfg, false, NULL));
    if (m) {
        body = bce_block(&m->funcs[0], "body");
        T_ASSERT(t, body != NULL && body->last != NULL);
        if (body && body->last) {
            T_ASSERT_EQ_INT(t, body->last->ops[0].kind, IROP_ICONST);
            T_ASSERT_EQ_INT(t, body->last->ops[0].a, 1);
        }
        T_ASSERT(t, ir_verify(fix.dc, m));
    }
    arena_free_all(&fix.arena);
}

void test_opt_bce_keeps_overshooting_step_check(TestCtx *t)
{
    BceFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];

    bce_fix_init(&fix);
    m = bce_parse(&fix, "func i32 @f() {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %more = icmp ult i32 %i, 10\n"
                        "    condbr %more, body(), exit()\n"
                        "body():\n"
                        "    %next = iadd i32 %i, 3\n"
                        "    %safe = icmp ult i32 %next, 10, bounds\n"
                        "    condbr %safe, loop(i32 %next), exit()\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n");
    report = tmpfile();
    T_ASSERT(t, report != NULL && m != NULL && ir_verify(fix.dc, m));
    if (m)
        T_ASSERT(t, !bce_run(m, &cfg, false, report));
    if (report) {
        bce_read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "bce_unproven") != NULL);
        fclose(report);
    }
    if (m) {
        T_ASSERT_EQ_INT(t, bce_count_op(&m->funcs[0], IR_ICMP), 2);
        T_ASSERT(t, ir_verify(fix.dc, m));
    }
    arena_free_all(&fix.arena);
}

void test_opt_bce_fwrapv_disables_signed_range_conclusion(TestCtx *t)
{
    BceFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];

    bce_fix_init(&fix);
    m = bce_parse(&fix, "func i32 @f() {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %more = icmp slt i32 %i, 4\n"
                        "    condbr %more, body(), exit()\n"
                        "body():\n"
                        "    %next = iadd nsw i32 %i, 1\n"
                        "    %safe = icmp sle i32 %next, 4, bounds\n"
                        "    condbr %safe, loop(i32 %next), trap()\n"
                        "trap():\n"
                        "    ret i32 99\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n");
    report = tmpfile();
    T_ASSERT(t, report != NULL && m != NULL && ir_verify(fix.dc, m));
    if (m)
        T_ASSERT(t, !bce_run(m, &cfg, true, report));
    if (report) {
        bce_read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "bce_wrap") != NULL);
        fclose(report);
    }
    if (m)
        T_ASSERT_EQ_INT(t, bce_count_op(&m->funcs[0], IR_ICMP), 2);
    arena_free_all(&fix.arena);
}

void test_opt_bce_subword_modular_crossing_bails_wrap(TestCtx *t)
{
    BceFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];

    bce_fix_init(&fix);
    m = bce_parse(&fix, "func i32 @f() {\n"
                        "entry():\n"
                        "    br loop(i8 250)\n"
                        "loop(i8 %i):\n"
                        "    %more = icmp ne i8 %i, 4\n"
                        "    condbr %more, body(), exit()\n"
                        "body():\n"
                        "    %next = iadd i8 %i, 3\n"
                        "    %safe = icmp ne i8 %next, 5, bounds\n"
                        "    condbr %safe, loop(i8 %next), trap()\n"
                        "trap():\n"
                        "    ret i32 99\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n");
    report = tmpfile();
    T_ASSERT(t, report != NULL && m != NULL && ir_verify(fix.dc, m));
    if (m)
        T_ASSERT(t, !bce_run(m, &cfg, false, report));
    if (report) {
        bce_read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "bce_wrap") != NULL);
        fclose(report);
    }
    if (m)
        T_ASSERT_EQ_INT(t, bce_count_op(&m->funcs[0], IR_ICMP), 2);
    arena_free_all(&fix.arena);
}

void test_opt_bce_uses_dominance_in_the_safe_direction(TestCtx *t)
{
    BceFix fix;
    IrModule *m;
    IrBlock *checked;
    IrBlock *body;
    OptConfig cfg;

    bce_fix_init(&fix);
    m = bce_parse(&fix, "func i32 @f(i32 %n) {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %more = icmp ult i32 %i, 4\n"
                        "    condbr %more, body(), exit()\n"
                        "body():\n"
                        "    %before = icmp ult i32 %i, %n\n"
                        "    br test()\n"
                        "test():\n"
                        "    %gate = icmp ult i32 %i, %n\n"
                        "    condbr %gate, checked(), latch()\n"
                        "checked():\n"
                        "    %after = icmp ult i32 %i, %n\n"
                        "    br latch()\n"
                        "latch():\n"
                        "    %next = iadd i32 %i, 1\n"
                        "    br loop(i32 %next)\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    T_ASSERT(t, m && bce_run(m, &cfg, false, NULL));
    if (m) {
        body = bce_block(&m->funcs[0], "body");
        checked = bce_block(&m->funcs[0], "checked");
        T_ASSERT(t, body != NULL && checked != NULL);
        if (body)
            T_ASSERT_EQ_INT(t, body->first->op, IR_ICMP);
        if (checked)
            T_ASSERT_EQ_INT(t, checked->first->op, IR_BR);
        T_ASSERT_EQ_INT(t, bce_count_op(&m->funcs[0], IR_ICMP), 3);
        T_ASSERT(t, ir_verify(fix.dc, m));
    }
    arena_free_all(&fix.arena);
}

void test_opt_bce_does_not_treat_loop_backedge_as_entry_fact(TestCtx *t)
{
    BceFix fix;
    IrModule *m;
    OptConfig cfg;

    bce_fix_init(&fix);
    m = bce_parse(&fix, "func i32 @f(i32 %n) {\n"
                        "entry():\n"
                        "    br loop(i32 0)\n"
                        "loop(i32 %i):\n"
                        "    %candidate = icmp ult i32 %i, %n, bounds\n"
                        "    %finite = icmp ult i32 %i, 4\n"
                        "    condbr %finite, body(), exit()\n"
                        "body():\n"
                        "    %next = iadd i32 %i, 1\n"
                        "    %gate = icmp ult i32 %i, %n\n"
                        "    condbr %gate, loop(i32 %next), exit()\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    T_ASSERT(t, m && !bce_run(m, &cfg, false, NULL));
    if (m) {
        T_ASSERT_EQ_INT(t, bce_count_op(&m->funcs[0], IR_ICMP), 3);
        T_ASSERT(t, ir_verify(fix.dc, m));
    }
    arena_free_all(&fix.arena);
}

void test_opt_bce_entry_self_loop_has_implicit_predecessor(TestCtx *t)
{
    BceFix fix;
    IrModule *m;
    OptConfig cfg;

    bce_fix_init(&fix);
    m = bce_parse(&fix, "func i32 @f(i32 %n) {\n"
                        "entry():\n"
                        "    %candidate = icmp ult i32 %n, 10, bounds\n"
                        "    %gate = icmp ult i32 %n, 10\n"
                        "    condbr %gate, body(), exit()\n"
                        "body():\n"
                        "    br exit()\n"
                        "exit():\n"
                        "    ret i32 0\n"
                        "}\n");
    T_ASSERT(t, m != NULL && ir_verify(fix.dc, m));
    /* The verifier normally rejects entry predecessors.  Mutate only after
     * establishing that the fixture was valid so this directly exercises
     * BCE's defensive treatment of an implicit invocation predecessor. */
    if (m)
        m->funcs[0].blocks[0].last->edges[0].target = (BlockId){1};
    T_ASSERT(t, m && !bce_run(m, &cfg, false, NULL));
    if (m)
        T_ASSERT_EQ_INT(t, bce_count_op(&m->funcs[0], IR_ICMP), 2);
    arena_free_all(&fix.arena);
}

void test_opt_bce_descriptor(TestCtx *t)
{
    T_ASSERT_EQ_STR(t, OPT_PASS_BCE.name, "bce");
    T_ASSERT(t, OPT_PASS_BCE.run == opt_bce);
    T_ASSERT_EQ_INT(t, OPT_PASS_BCE.pinned_policy, PASS_PINNED_EXACT);
}
