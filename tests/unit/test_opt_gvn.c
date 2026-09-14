#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

bool opt_gvn(IrModule *m, const OptConfig *cfg);

typedef struct {
    Arena arena;
    DiagCtx *dc;
} GvnFix;

static void gvn_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void gvn_fix_init(GvnFix *f)
{
    DiagSink sink = {gvn_silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *gvn_parse(GvnFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<gvn-test>");
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

static void gvn_memory_limit_source(Buf *src, u32 padding)
{
    u32 i;

    buf_init(src);
    buf_printf(src, "func i32 @f(ptr %%p, i32 %%x) {\n"
                    "entry():\n"
                    "    store i32 7, %%p, align 4, etype i32\n");
    for (i = 0; i < padding; i++)
        buf_printf(src, "    %%v%u = iadd i32 %%x, 1\n", i);
    buf_printf(src, "    %%r = load i32, %%p, align 4, etype i32\n"
                    "    ret i32 %%r\n"
                    "}\n");
    buf_push_u8(src, 0);
}

void test_opt_gvn_memory_work_limit_exact_boundary(TestCtx *t)
{
    static const u32 padding[] = {4093, 4094};
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(padding); i++) {
        GvnFix f;
        IrModule *m;
        OptConfig cfg;
        Buf src;
        FILE *report;
        char log[256];

        gvn_fix_init(&f);
        gvn_memory_limit_source(&src, padding[i]);
        m = gvn_parse(&f, (const char *)src.data);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        report = tmpfile();
        T_ASSERT(t, report != NULL);
        opt_config_init(&cfg, OPT_O2);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, m && opt_gvn(m, &cfg));
        if (m) {
            T_ASSERT_EQ_INT(t, count_op(m, IR_LOAD), i == 0 ? 0 : 1);
            T_ASSERT_EQ_INT(t, count_op(m, IR_IADD), 1);
            T_ASSERT(t, ir_verify(f.dc, m));
        }
        if (report) {
            read_report(report, log, sizeof(log));
            T_ASSERT_EQ_STR(
                t, log,
                i == 0 ? "" : "bail: gvn gvn_memory_work_limit func=@f\n");
            fclose(report);
        }
        buf_free(&src);
        arena_free_all(&f.arena);
    }
}

void test_opt_gvn_eliminates_dominating_pure_expression(TestCtx *t)
{
    GvnFix f;
    IrModule *m;
    OptConfig cfg;

    gvn_fix_init(&f);
    m = gvn_parse(&f, "func i32 @f(i32 %x, i32 %y) {\n"
                      "entry():\n"
                      "    %a = iadd i32 %x, %y\n"
                      "    br next()\n"
                      "next():\n"
                      "    %b = iadd i32 %y, %x\n"
                      "    ret i32 %b\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_gvn(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_IADD), 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_gvn_never_uses_nondominating_leader(TestCtx *t)
{
    GvnFix f;
    IrModule *m;
    OptConfig cfg;

    gvn_fix_init(&f);
    m = gvn_parse(&f, "func i32 @f(i32 %c, i32 %x, i32 %y) {\n"
                      "entry():\n"
                      "    condbr %c, left(), right()\n"
                      "left():\n"
                      "    %a = iadd i32 %x, %y\n"
                      "    br join(i32 %a)\n"
                      "right():\n"
                      "    %b = iadd i32 %x, %y\n"
                      "    br join(i32 %b)\n"
                      "join(i32 %v):\n"
                      "    ret i32 %v\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && !opt_gvn(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_IADD), 2);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_gvn_keeps_block_parameters_fresh(TestCtx *t)
{
    GvnFix f;
    IrModule *m;
    OptConfig cfg;

    gvn_fix_init(&f);
    m = gvn_parse(&f, "func i32 @f(i32 %x) {\n"
                      "entry():\n"
                      "    br join(i32 %x, i32 %x)\n"
                      "join(i32 %a, i32 %b):\n"
                      "    %u = iadd i32 %a, 1\n"
                      "    %v = iadd i32 %b, 1\n"
                      "    %r = iadd i32 %u, %v\n"
                      "    ret i32 %r\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && !opt_gvn(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_IADD), 3);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_gvn_loads_cross_noalias_but_not_mayalias_store(TestCtx *t)
{
    static const char *const sources[] = {
        "func i32 @f(ptr restrict %p, ptr restrict %q) {\n"
        "entry():\n"
        "    %a = load i32, %p, align 4, etype i32\n"
        "    store i32 9, %q, align 4, etype i32\n"
        "    %b = load i32, %p, align 4, etype i32\n"
        "    %r = iadd i32 %a, %b\n"
        "    ret i32 %r\n"
        "}\n",
        "func i32 @f(ptr %p, ptr %q) {\n"
        "entry():\n"
        "    %a = load i32, %p, align 4, etype i32\n"
        "    store i32 9, %q, align 4, etype i32\n"
        "    %b = load i32, %p, align 4, etype i32\n"
        "    %r = iadd i32 %a, %b\n"
        "    ret i32 %r\n"
        "}\n",
    };
    u32 i;

    for (i = 0; i < 2; i++) {
        GvnFix f;
        IrModule *m;
        OptConfig cfg;
        FILE *report;
        char log[256];

        gvn_fix_init(&f);
        m = gvn_parse(&f, sources[i]);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        report = tmpfile();
        T_ASSERT(t, report != NULL);
        opt_config_init(&cfg, OPT_O2);
        cfg.verify_after_each = true;
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT_EQ_INT(t, m && opt_gvn(m, &cfg), i == 0);
        if (m) {
            T_ASSERT_EQ_INT(t, count_op(m, IR_LOAD), i == 0 ? 1 : 2);
            T_ASSERT(t, ir_verify(f.dc, m));
        }
        if (report) {
            read_report(report, log, sizeof(log));
            T_ASSERT_EQ_STR(t, log,
                            i == 0
                                ? ""
                                : "bail: gvn "
                                  "gvn_load_intervening_may_store func=@f\n");
            fclose(report);
        }
        arena_free_all(&f.arena);
    }
}

void test_opt_gvn_forwards_store_to_load(TestCtx *t)
{
    GvnFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *ret;

    gvn_fix_init(&f);
    m = gvn_parse(&f, "func i32 @f(ptr %p, i32 %x) {\n"
                      "entry():\n"
                      "    store i32 %x, %p, align 4, etype i32\n"
                      "    %v = load i32, %p, align 4, etype i32\n"
                      "    ret i32 %v\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_gvn(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_LOAD), 0);
        ret = m->funcs[0].blocks[0].last;
        T_ASSERT_EQ_INT(t, ret->ops[0].kind, IROP_VALUE);
        T_ASSERT_EQ_INT(t, ret->ops[0].a, 2);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_gvn_keeps_opposite_correlated_select_load(TestCtx *t)
{
    GvnFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *ret;

    gvn_fix_init(&f);
    m = gvn_parse(&f, "func i32 @f(i32 %choose) {\n"
                      "entry():\n"
                      "    %a = alloca 8, align 8\n"
                      "    %a4 = ptradd %a, 4\n"
                      "    %p = select %choose, ptr %a, %a4\n"
                      "    %q = select %choose, ptr %a4, %a\n"
                      "    store i32 11, %p, align 4, etype i32\n"
                      "    store i32 22, %q, align 4, etype i32\n"
                      "    %v = load i32, %p, align 4, etype i32\n"
                      "    ret i32 %v\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    if (m) {
        (void)opt_gvn(m, &cfg);
        T_ASSERT_EQ_INT(t, count_op(m, IR_LOAD), 1);
        ret = m->funcs[0].blocks[0].last;
        T_ASSERT_EQ_INT(t, ret->ops[0].kind, IROP_VALUE);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_gvn_volatile_and_atomic_operations_are_barriers(TestCtx *t)
{
    static const char *const barriers[] = {
        "    %v = load i32, %q, align 4, volatile, etype i32\n",
        "    %v = atomicrmw xchg i32 %q, 1, seq_cst\n",
    };
    u32 i;

    for (i = 0; i < 2; i++) {
        GvnFix f;
        IrModule *m;
        OptConfig cfg;
        FILE *report;
        char src[1024], log[256];

        (void)snprintf(src, sizeof(src),
                       "func i32 @f(ptr %%p, ptr %%q) {\n"
                       "entry():\n"
                       "    %%a = load i32, %%p, align 4, etype i32\n"
                       "%s"
                       "    %%b = load i32, %%p, align 4, etype i32\n"
                       "    ret i32 %%b\n"
                       "}\n",
                       barriers[i]);
        gvn_fix_init(&f);
        m = gvn_parse(&f, src);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        report = tmpfile();
        T_ASSERT(t, report != NULL);
        opt_config_init(&cfg, OPT_O2);
        cfg.verify_after_each = true;
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, m && !opt_gvn(m, &cfg));
        if (m) {
            T_ASSERT_EQ_INT(t, count_op(m, IR_LOAD), i == 0 ? 3 : 2);
            T_ASSERT_EQ_INT(t, count_op(m, IR_ATOMICRMW), i == 0 ? 0 : 1);
            T_ASSERT(t, ir_verify(f.dc, m));
        }
        if (report) {
            read_report(report, log, sizeof(log));
            T_ASSERT_EQ_STR(t, log, "bail: gvn gvn_barrier func=@f\n");
            fclose(report);
        }
        arena_free_all(&f.arena);
    }
}

void test_opt_gvn_load_scope_is_one_direct_dominator(TestCtx *t)
{
    static const char *const sources[] = {
        "func i32 @f(ptr %p) {\n"
        "entry():\n"
        "    %a = load i32, %p, align 4, etype i32\n"
        "    br use()\n"
        "use():\n"
        "    %b = load i32, %p, align 4, etype i32\n"
        "    ret i32 %b\n"
        "}\n",
        "func i32 @f(ptr %p) {\n"
        "entry():\n"
        "    %a = load i32, %p, align 4, etype i32\n"
        "    br middle()\n"
        "middle():\n"
        "    br use()\n"
        "use():\n"
        "    %b = load i32, %p, align 4, etype i32\n"
        "    ret i32 %b\n"
        "}\n",
    };
    u32 i;

    for (i = 0; i < 2; i++) {
        GvnFix f;
        IrModule *m;
        OptConfig cfg;
        FILE *report;
        char log[256];

        gvn_fix_init(&f);
        m = gvn_parse(&f, sources[i]);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        report = tmpfile();
        T_ASSERT(t, report != NULL);
        opt_config_init(&cfg, OPT_O2);
        cfg.verify_after_each = true;
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT_EQ_INT(t, m && opt_gvn(m, &cfg), i == 0);
        if (m) {
            T_ASSERT_EQ_INT(t, count_op(m, IR_LOAD), i == 0 ? 1 : 2);
            T_ASSERT(t, ir_verify(f.dc, m));
        }
        if (report) {
            read_report(report, log, sizeof(log));
            T_ASSERT_EQ_STR(t, log,
                            i == 0
                                ? ""
                                : "bail: gvn "
                                  "gvn_load_intervening_may_store func=@f\n");
            fclose(report);
        }
        arena_free_all(&f.arena);
    }
}

void test_opt_gvn_preserves_call_operand_abi_annotation(TestCtx *t)
{
    GvnFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *call;

    gvn_fix_init(&f);
    m = gvn_parse(&f, "func void @f(ptr %p) {\n"
                      "entry():\n"
                      "    %a = ptradd %p, 0\n"
                      "    %b = ptradd %p, 0\n"
                      "    call void @sink(ptr %b byval(16))\n"
                      "    ret\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_gvn(m, &cfg));
    if (m) {
        call = m->funcs[0].blocks[0].first->next;
        T_ASSERT_EQ_INT(t, call->op, IR_CALL);
        T_ASSERT_EQ_INT(t, call->ops[0].kind, IROP_VALUE);
        T_ASSERT_EQ_INT(t, ir_arg_kind(call->ops[0].b), IR_ARG_BYVAL);
        T_ASSERT_EQ_INT(t, ir_arg_size(call->ops[0].b), 16);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_gvn_preserves_undef_tainted_values(TestCtx *t)
{
    static const char *const sources[] = {
        "func i32 @f(i32 %x) {\n"
        "entry():\n"
        "    %u = sdiv i32 %x, 0\n"
        "    %a = iadd i32 %u, 1\n"
        "    br next()\n"
        "next():\n"
        "    %b = iadd i32 %u, 1\n"
        "    %r = iadd i32 %a, %b\n"
        "    ret i32 %r\n"
        "}\n",
        "func i32 @f(ptr %p) {\n"
        "entry():\n"
        "    store i32 undef, %p, align 4, etype i32\n"
        "    %v = load i32, %p, align 4, etype i32\n"
        "    ret i32 %v\n"
        "}\n",
    };
    u32 i;

    for (i = 0; i < 2; i++) {
        GvnFix f;
        IrModule *m;
        OptConfig cfg;

        gvn_fix_init(&f);
        m = gvn_parse(&f, sources[i]);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        opt_config_init(&cfg, OPT_O2);
        cfg.verify_after_each = true;
        T_ASSERT(t, m && !opt_gvn(m, &cfg));
        if (m) {
            T_ASSERT_EQ_INT(t, count_op(m, IR_IADD), i == 0 ? 3 : 0);
            T_ASSERT_EQ_INT(t, count_op(m, IR_LOAD), i == 0 ? 0 : 1);
            T_ASSERT(t, ir_verify(f.dc, m));
        }
        arena_free_all(&f.arena);
    }
}
