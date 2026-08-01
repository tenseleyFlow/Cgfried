#include "opt/opt.h"
#include "unit.h"

#include <stdio.h>
#include <string.h>

typedef struct UnswitchFix {
    Arena arena;
    DiagCtx *dc;
} UnswitchFix;

static void silent_sink(void *user, const Diag *diag, const DiagCtx *dc)
{
    (void)user;
    (void)diag;
    (void)dc;
}

static void fix_init(UnswitchFix *fix)
{
    DiagSink sink = {silent_sink, NULL};

    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    diag_set_sink(fix->dc, sink);
}

static IrModule *parse(UnswitchFix *fix, const char *source)
{
    return ir_parse_module(&fix->arena, fix->dc, source, "<unswitch-test>");
}

static u32 count_op(const IrFunc *f, IrOp op)
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

static u32 count_literal_cond(const IrFunc *f, u64 value)
{
    u32 bi, count = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *term = f->blocks[bi].last;

        if (term && term->op == IR_CONDBR && term->nops == 1 &&
            term->ops[0].kind == IROP_ICONST && term->ops[0].a == value)
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

static const char *parameter_loop(bool with_volatile)
{
    return with_volatile
               ? "func void @f(i32 %flag, ptr %p) {\n"
                 "entry():\n"
                 "    br head(i32 0)\n"
                 "head(i32 %i):\n"
                 "    %more = icmp ult i32 %i, 4\n"
                 "    condbr %more, body(), exit()\n"
                 "body():\n"
                 "    %v = load i32, %p, align 4, volatile, etype i32\n"
                 "    condbr %flag, yes(), no()\n"
                 "yes():\n"
                 "    br latch()\n"
                 "no():\n"
                 "    br latch()\n"
                 "latch():\n"
                 "    %next = iadd i32 %i, 1\n"
                 "    br head(i32 %next)\n"
                 "exit():\n"
                 "    ret\n"
                 "}\n"
               : "func i32 @f(i32 %flag) {\n"
                 "entry():\n"
                 "    br head(i32 0)\n"
                 "head(i32 %i):\n"
                 "    %more = icmp ult i32 %i, 4\n"
                 "    condbr %more, body(), exit()\n"
                 "body():\n"
                 "    condbr %flag, yes(), no()\n"
                 "yes():\n"
                 "    br latch()\n"
                 "no():\n"
                 "    br latch()\n"
                 "latch():\n"
                 "    %next = iadd i32 %i, 1\n"
                 "    br head(i32 %next)\n"
                 "exit():\n"
                 "    ret i32 %i\n"
                 "}\n";
}

void test_opt_unswitch_parameter_condition_clones_both_versions(TestCtx *t)
{
    UnswitchFix fix;
    IrModule *m;
    OptConfig cfg;

    fix_init(&fix);
    m = parse(&fix, parameter_loop(false));
    T_ASSERT(t, m && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_unswitch(m, &cfg));
    if (m) {
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, count_literal_cond(&m->funcs[0], 0), 1);
        T_ASSERT_EQ_INT(t, count_literal_cond(&m->funcs[0], 1), 1);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_IADD), 2);
        T_ASSERT_EQ_INT(t, m->funcs[0].blocks[0].last->op, IR_CONDBR);
        T_ASSERT_EQ_INT(t, m->funcs[0].blocks[0].last->ops[0].kind, IROP_VALUE);
    }
    arena_free_all(&fix.arena);
}

void test_opt_unswitch_safe_internal_dag_is_hoisted(TestCtx *t)
{
    UnswitchFix fix;
    IrModule *m;
    OptConfig cfg;

    fix_init(&fix);
    m = parse(&fix, "func i32 @f(i32 %x) {\n"
                    "entry():\n"
                    "    br head(i32 0)\n"
                    "head(i32 %i):\n"
                    "    %more = icmp ult i32 %i, 3\n"
                    "    condbr %more, body(), exit()\n"
                    "body():\n"
                    "    %stable = icmp ne i32 %x, 0\n"
                    "    condbr %stable, yes(), no()\n"
                    "yes():\n"
                    "    br latch()\n"
                    "no():\n"
                    "    br latch()\n"
                    "latch():\n"
                    "    %next = iadd i32 %i, 1\n"
                    "    br head(i32 %next)\n"
                    "exit():\n"
                    "    ret i32 %i\n"
                    "}\n");
    T_ASSERT(t, m && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O3);
    T_ASSERT(t, m && opt_unswitch(m, &cfg));
    if (m) {
        const IrBlock *entry = &m->funcs[0].blocks[0];

        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, entry->ninsts, 2);
        T_ASSERT_EQ_INT(t, entry->first->op, IR_ICMP);
        T_ASSERT_EQ_INT(t, entry->last->op, IR_CONDBR);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_ICMP), 5);
    }
    arena_free_all(&fix.arena);
}

void test_opt_unswitch_guarded_division_is_not_speculated(TestCtx *t)
{
    UnswitchFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report = tmpfile();
    char text[512];

    fix_init(&fix);
    m = parse(&fix, "func i32 @f(i32 %x, i32 %d) {\n"
                    "entry():\n"
                    "    br head(i32 0)\n"
                    "head(i32 %i):\n"
                    "    %more = icmp ult i32 %i, 0\n"
                    "    condbr %more, body(), exit()\n"
                    "body():\n"
                    "    %q = udiv i32 %x, %d\n"
                    "    %stable = icmp ne i32 %q, 0\n"
                    "    condbr %stable, yes(), no()\n"
                    "yes():\n"
                    "    br latch()\n"
                    "no():\n"
                    "    br latch()\n"
                    "latch():\n"
                    "    %next = iadd i32 %i, 1\n"
                    "    br head(i32 %next)\n"
                    "exit():\n"
                    "    ret i32 %i\n"
                    "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.bail_log = true;
    cfg.report = report;
    if (m)
        (void)opt_unswitch(m, &cfg);
    if (report) {
        read_report(report, text, sizeof(text));
        T_ASSERT(t, strstr(text, "unswitch_unspeculatable") != NULL);
        fclose(report);
    }
    if (m) {
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_UDIV), 1);
    }
    arena_free_all(&fix.arena);
}

void test_opt_unswitch_zero_trip_nsw_dag_is_not_speculated(TestCtx *t)
{
    UnswitchFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report = tmpfile();
    char text[512];

    fix_init(&fix);
    m = parse(&fix, "func i32 @f(i32 %x) {\n"
                    "entry():\n"
                    "    br head(i32 0)\n"
                    "head(i32 %i):\n"
                    "    %more = icmp ult i32 %i, 0\n"
                    "    condbr %more, body(), exit()\n"
                    "body():\n"
                    "    %sum = iadd nsw i32 %x, 1\n"
                    "    %stable = icmp ne i32 %sum, 0\n"
                    "    condbr %stable, yes(), no()\n"
                    "yes():\n"
                    "    br latch()\n"
                    "no():\n"
                    "    br latch()\n"
                    "latch():\n"
                    "    %next = iadd i32 %i, 1\n"
                    "    br head(i32 %next)\n"
                    "exit():\n"
                    "    ret i32 %i\n"
                    "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.bail_log = true;
    cfg.report = report;
    if (m)
        (void)opt_unswitch(m, &cfg);
    if (report) {
        read_report(report, text, sizeof(text));
        T_ASSERT(t, strstr(text, "unswitch_unspeculatable") != NULL);
        fclose(report);
    }
    if (m) {
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_IADD), 2);
        T_ASSERT_EQ_INT(t, count_literal_cond(&m->funcs[0], 0), 0);
        T_ASSERT_EQ_INT(t, count_literal_cond(&m->funcs[0], 1), 0);
    }
    arena_free_all(&fix.arena);
}

void test_opt_unswitch_volatile_clone_fidelity(TestCtx *t)
{
    UnswitchFix fix;
    IrModule *m;
    OptConfig cfg;
    const Pass *passes[] = {&OPT_PASS_UNSWITCH};

    fix_init(&fix);
    m = parse(&fix, parameter_loop(true));
    T_ASSERT(t, m && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_run_pass_sequence(m, &cfg, passes, 1));
    if (m) {
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_LOAD), 2);
        T_ASSERT_EQ_INT(t, count_literal_cond(&m->funcs[0], 0), 1);
        T_ASSERT_EQ_INT(t, count_literal_cond(&m->funcs[0], 1), 1);
    }
    arena_free_all(&fix.arena);
}

void test_opt_unswitch_growth_cap_bails_before_mutation(TestCtx *t)
{
    UnswitchFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report = tmpfile();
    char text[512];

    fix_init(&fix);
    m = parse(&fix, "func i32 @f(i32 %x) {\n"
                    "entry():\n"
                    "    br head(i32 0)\n"
                    "head(i32 %i):\n"
                    "    %more = icmp ult i32 %i, 3\n"
                    "    condbr %more, body(), exit()\n"
                    "body():\n"
                    "    %a = iadd i32 %x, 1\n"
                    "    %b = imul i32 %a, 3\n"
                    "    %stable = icmp ne i32 %b, 0\n"
                    "    condbr %stable, yes(), no()\n"
                    "yes():\n"
                    "    br latch()\n"
                    "no():\n"
                    "    br latch()\n"
                    "latch():\n"
                    "    %next = iadd i32 %i, 1\n"
                    "    br head(i32 %next)\n"
                    "exit():\n"
                    "    ret i32 %i\n"
                    "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.bail_log = true;
    cfg.report = report;
    if (m)
        (void)opt_unswitch(m, &cfg);
    if (report) {
        read_report(report, text, sizeof(text));
        T_ASSERT(t, strstr(text, "unswitch_growth") != NULL);
        fclose(report);
    }
    if (m) {
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_IMUL), 1);
    }
    arena_free_all(&fix.arena);
}

void test_opt_unswitch_respects_level_and_disable_toggle(TestCtx *t)
{
    UnswitchFix fix;
    IrModule *m;
    OptConfig cfg;

    fix_init(&fix);
    m = parse(&fix, parameter_loop(false));
    T_ASSERT(t, m && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O2);
    T_ASSERT(t, m && !opt_unswitch(m, &cfg));
    opt_config_init(&cfg, OPT_O3);
    cfg.disable_unswitch = true;
    T_ASSERT(t, m && !opt_unswitch(m, &cfg));
    if (m)
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_IADD), 1);
    arena_free_all(&fix.arena);
}

void test_opt_unswitch_one_transform_per_function_cap(TestCtx *t)
{
    UnswitchFix fix;
    IrModule *m;
    OptConfig cfg;

    fix_init(&fix);
    m = parse(&fix, "func i32 @f(i32 %a, i32 %b) {\n"
                    "entry():\n"
                    "    br h1(i32 0)\n"
                    "h1(i32 %i):\n"
                    "    %m1 = icmp ult i32 %i, 2\n"
                    "    condbr %m1, body1(), after1()\n"
                    "body1():\n"
                    "    condbr %a, yes1(), no1()\n"
                    "yes1():\n"
                    "    br latch1()\n"
                    "no1():\n"
                    "    br latch1()\n"
                    "latch1():\n"
                    "    %n1 = iadd i32 %i, 1\n"
                    "    br h1(i32 %n1)\n"
                    "after1():\n"
                    "    br h2(i32 0)\n"
                    "h2(i32 %j):\n"
                    "    %m2 = icmp ult i32 %j, 2\n"
                    "    condbr %m2, body2(), exit()\n"
                    "body2():\n"
                    "    condbr %b, yes2(), no2()\n"
                    "yes2():\n"
                    "    br latch2()\n"
                    "no2():\n"
                    "    br latch2()\n"
                    "latch2():\n"
                    "    %n2 = iadd i32 %j, 1\n"
                    "    br h2(i32 %n2)\n"
                    "exit():\n"
                    "    ret i32 %j\n"
                    "}\n");
    T_ASSERT(t, m && ir_verify(fix.dc, m));
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_unswitch(m, &cfg));
    if (m) {
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, count_literal_cond(&m->funcs[0], 0), 1);
        T_ASSERT_EQ_INT(t, count_literal_cond(&m->funcs[0], 1), 1);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_IADD), 3);
    }
    arena_free_all(&fix.arena);
}
