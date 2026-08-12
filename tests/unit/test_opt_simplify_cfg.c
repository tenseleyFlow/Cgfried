#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} CfgFix;

static void silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void fix_init(CfgFix *f)
{
    DiagSink sink = {silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *parse(CfgFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<simplify-cfg-test>");
}

static u32 count_op(const IrModule *m, IrOp op)
{
    u32 fi, bi, count = 0;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next)
                if (in->op == op)
                    count++;
        }
    return count;
}

static void read_report(FILE *report, char *out, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(out, 1, cap - 1, report);
    out[n] = '\0';
}

void test_cfg_const_condbr_logs_removed_span(TestCtx *t)
{
    CfgFix f;
    IrModule *m;
    IrFunc *fn;
    OptConfig cfg;
    Span span = {0};
    const CfgRemoved *removed;
    u32 nremoved = 0;

    fix_init(&f);
    m = parse(&f, "func i32 @f() {\n"
                  "entry():\n"
                  "    condbr 1, yes(), no()\n"
                  "yes():\n"
                  "    ret i32 7\n"
                  "no():\n"
                  "    ret i32 9\n"
                  "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    fn = m ? &m->funcs[0] : NULL;
    span.file_id = 7;
    span.line = 41;
    span.col = 3;
    span.len = 6;
    if (fn)
        fn->blocks[2].first->loc = ir_intern_span(m, span);
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_simplify_cfg(m, &cfg));
    if (m) {
        removed = opt_cfg_removed_log(fn, &nremoved);
        T_ASSERT_EQ_INT(t, nremoved, 1);
        T_ASSERT(t, removed != NULL);
        if (removed) {
            T_ASSERT_EQ_INT(t, removed[0].block.v, 3);
            T_ASSERT_EQ_INT(t, removed[0].loc.file_id, 7);
            T_ASSERT_EQ_INT(t, removed[0].loc.line, 41);
        }
        T_ASSERT_EQ_INT(t, fn->nblocks, 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_CONDBR), 0);
        T_ASSERT(t, ir_verify(f.dc, m));
        T_ASSERT(t, !opt_simplify_cfg(m, &cfg));
        T_ASSERT_EQ_INT(t, opt_cfg_removed_log(fn, &nremoved), removed);
        T_ASSERT_EQ_INT(t, nremoved, 1);
    }
    arena_free_all(&f.arena);
}

void test_cfg_const_switch_preserves_live_cases(TestCtx *t)
{
    CfgFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func i32 @constant() {\n"
                  "entry():\n"
                  "    switch i32 20, def(), 10: ten(), 20: twenty()\n"
                  "def():\n"
                  "    ret i32 0\n"
                  "ten():\n"
                  "    ret i32 1\n"
                  "twenty():\n"
                  "    ret i32 2\n"
                  "}\n"
                  "func i32 @live(i32 %x) {\n"
                  "entry():\n"
                  "    switch i32 %x, def(), -7: neg(), 99: pos()\n"
                  "def():\n"
                  "    ret i32 0\n"
                  "neg():\n"
                  "    ret i32 1\n"
                  "pos():\n"
                  "    ret i32 2\n"
                  "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && opt_simplify_cfg(m, &cfg));
    if (m) {
        IrInst *sw = m->funcs[1].blocks[0].last;

        T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, 1);
        T_ASSERT_EQ_INT(t, sw->op, IR_SWITCH);
        T_ASSERT_EQ_INT(t, sw->nedges, 3);
        T_ASSERT_EQ_INT(t, sw->edges[1].case_val, -7);
        T_ASSERT_EQ_INT(t, sw->edges[2].case_val, 99);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_cfg_merges_straight_line_and_forwards_args(TestCtx *t)
{
    CfgFix f;
    IrModule *m;
    OptConfig cfg;
    IrInst *mul;

    fix_init(&f);
    m = parse(&f, "func i32 @f(i32 %x) {\n"
                  "entry():\n"
                  "    %a = iadd i32 %x, 1\n"
                  "    br next(i32 %a)\n"
                  "next(i32 %p):\n"
                  "    %r = imul i32 %p, 2\n"
                  "    ret i32 %r\n"
                  "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && opt_simplify_cfg(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, 1);
        mul = m->funcs[0].blocks[0].first->next;
        T_ASSERT_EQ_INT(t, mul->op, IR_IMUL);
        T_ASSERT_EQ_INT(t, mul->ops[0].kind, IROP_VALUE);
        T_ASSERT_EQ_INT(t, mul->ops[0].a, 2);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_cfg_forward_keeps_call_arg_provenance(TestCtx *t)
{
    CfgFix f;
    IrModule *m;
    OptConfig cfg;
    IrInst *call;

    fix_init(&f);
    m = parse(&f,
              "func i32 @f(i32 %x) {\n"
              "entry():\n"
              "    br next(i32 %x)\n"
              "next(i32 %p):\n"
              "    %r = call i32 @sink(ptr @fmt, i32 %p anon, i32 7 anon) va\n"
              "    ret i32 %r\n"
              "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_simplify_cfg(m, &cfg));
    if (m) {
        call = m->funcs[0].blocks[0].first;
        T_ASSERT_EQ_INT(t, call->op, IR_CALL);
        T_ASSERT_EQ_INT(t, call->ops[0].argflags, 0);
        T_ASSERT_EQ_INT(t, call->ops[1].argflags, IROPF_ANON);
        T_ASSERT_EQ_INT(t, call->ops[2].argflags, IROPF_ANON);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_simplify_cfg_collapses_pure_diamond_to_select(TestCtx *t)
{
    CfgFix f;
    IrModule *m;
    OptConfig cfg;
    Span cond_span = {0};
    Span arm_span = {0};

    fix_init(&f);
    m = parse(&f, "func i32 @f(i32 %c, i32 %x) {\n"
                  "entry():\n"
                  "    condbr %c, yes(), no()\n"
                  "yes():\n"
                  "    %a = iadd i32 %x, 1\n"
                  "    br join(i32 %a)\n"
                  "no():\n"
                  "    %b = isub i32 %x, 1\n"
                  "    br join(i32 %b)\n"
                  "join(i32 %v):\n"
                  "    ret i32 %v\n"
                  "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    if (m) {
        cond_span.file_id = 8;
        cond_span.line = 11;
        cond_span.col = 5;
        cond_span.len = 2;
        arm_span.file_id = 8;
        arm_span.line = 13;
        arm_span.col = 9;
        arm_span.len = 4;
        m->funcs[0].blocks[0].last->loc = ir_intern_span(m, cond_span);
        m->funcs[0].blocks[1].first->loc = ir_intern_span(m, arm_span);
    }
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_simplify_cfg(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_SELECT), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_CONDBR), 0);
        T_ASSERT_EQ_INT(t, ir_inst_span(m, m->funcs[0].blocks[0].first).line,
                        13);
        {
            const IrInst *in;

            for (in = m->funcs[0].blocks[0].first; in && in->op != IR_SELECT;
                 in = in->next)
                ;
            T_ASSERT(t, in != NULL);
            if (in)
                T_ASSERT_EQ_INT(t, ir_inst_span(m, in).line, 11);
        }
        T_ASSERT(t, ir_verify(f.dc, m));
        T_ASSERT(t, !opt_simplify_cfg(m, &cfg));
    }
    arena_free_all(&f.arena);
}

void test_cfg_fp_diamond_requires_complete_fast_math_bundle(TestCtx *t)
{
    u32 i;

    for (i = 0; i < 3; i++) {
        CfgFix f;
        IrModule *m;
        OptConfig cfg;
        bool fast = i == 2;

        fix_init(&f);
        m = parse(&f, "func f64 @f(i32 %c, f64 %x) {\n"
                      "entry():\n"
                      "    condbr %c, yes(), no()\n"
                      "yes():\n"
                      "    %a = fadd f64 %x, 0x3FF0000000000000\n"
                      "    br join(f64 %a)\n"
                      "no():\n"
                      "    %b = fsub f64 %x, 0x3FF0000000000000\n"
                      "    br join(f64 %b)\n"
                      "join(f64 %v):\n"
                      "    ret f64 %v\n"
                      "}\n");
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        opt_config_init(&cfg, fast ? OPT_OFAST : OPT_O1);
        if (i == 1)
            cfg.fast_math.reassoc = true;
        T_ASSERT_EQ_INT(t, m && opt_simplify_cfg(m, &cfg), fast);
        if (m) {
            T_ASSERT_EQ_INT(t, count_op(m, IR_SELECT), fast ? 1 : 0);
            T_ASSERT_EQ_INT(t, count_op(m, IR_CONDBR), fast ? 0 : 1);
            T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, fast ? 1 : 4);
            T_ASSERT(t, ir_verify(f.dc, m));
        }
        arena_free_all(&f.arena);
    }
}

void test_cfg_unspeculatable_diamonds_exact_bail(TestCtx *t)
{
    static const char *const bodies[] = {
        "    %a = load i32, %p, align 4\n",
        "    %a = sdiv i32 %x, 3\n",
        "    %a = call i32 @side(i32 %x)\n",
    };
    u32 i;

    for (i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
        CfgFix f;
        IrModule *m;
        OptConfig cfg;
        FILE *report;
        char src[1024];
        char log[512];
        const char *hit;

        fix_init(&f);
        snprintf(src, sizeof(src),
                 "func i32 @f(i32 %%c, i32 %%x, ptr %%p) {\n"
                 "entry():\n"
                 "    condbr %%c, yes(), no()\n"
                 "yes():\n"
                 "%s"
                 "    br join(i32 %%a)\n"
                 "no():\n"
                 "    %%b = iadd i32 %%x, 1\n"
                 "    br join(i32 %%b)\n"
                 "join(i32 %%v):\n"
                 "    ret i32 %%v\n"
                 "}\n",
                 bodies[i]);
        m = parse(&f, src);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        report = tmpfile();
        T_ASSERT(t, report != NULL);
        opt_config_init(&cfg, OPT_O1);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, m && !opt_simplify_cfg(m, &cfg));
        if (m) {
            T_ASSERT_EQ_INT(t, count_op(m, IR_SELECT), 0);
            T_ASSERT_EQ_INT(t, count_op(m, IR_CONDBR), 1);
            T_ASSERT(t, ir_verify(f.dc, m));
        }
        if (report) {
            read_report(report, log, sizeof(log));
            hit = strstr(log, "bail: simplify_cfg cfg_select_unspeculatable "
                              "func=@f\n");
            T_ASSERT(t, hit != NULL);
            T_ASSERT(t, hit && strstr(hit + strlen("bail: simplify_cfg "
                                                   "cfg_select_unspeculatable"),
                                      "cfg_select_unspeculatable") == NULL);
            fclose(report);
        }
        arena_free_all(&f.arena);
    }
}
