#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} CseFix;

static void cse_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void cse_fix_init(CseFix *f)
{
    DiagSink sink = {cse_silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *cse_parse(CseFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<cse-test>");
}

static u32 cse_count_op(const IrModule *m, IrOp op)
{
    u32 fi, bi, n = 0;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next)
                if (in->op == op)
                    n++;
        }
    return n;
}

void test_opt_cse_eliminates_commuted_pure_op_in_one_block(TestCtx *t)
{
    CseFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *ret;

    cse_fix_init(&f);
    m = cse_parse(&f, "func i32 @f(i32 %x, i32 %y) {\n"
                      "entry():\n"
                      "    %a = iadd i32 %x, %y\n"
                      "    %b = iadd i32 %y, %x\n"
                      "    ret i32 %b\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_cse(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, cse_count_op(m, IR_IADD), 1);
        ret = m->funcs[0].blocks[0].last;
        T_ASSERT_EQ_INT(t, ret->ops[0].kind, IROP_VALUE);
        T_ASSERT_EQ_INT(t, ret->ops[0].a, 3);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_cse_is_block_local(TestCtx *t)
{
    CseFix f;
    IrModule *m;
    OptConfig cfg;

    cse_fix_init(&f);
    m = cse_parse(&f, "func i32 @f(i32 %x, i32 %y) {\n"
                      "entry():\n"
                      "    %a = iadd i32 %x, %y\n"
                      "    br next()\n"
                      "next():\n"
                      "    %b = iadd i32 %x, %y\n"
                      "    ret i32 %b\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && !opt_cse(m, &cfg));
    if (m)
        T_ASSERT_EQ_INT(t, cse_count_op(m, IR_IADD), 2);
    arena_free_all(&f.arena);
}

void test_opt_cse_never_eliminates_load_across_store(TestCtx *t)
{
    CseFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];
    size_t n;

    cse_fix_init(&f);
    m = cse_parse(&f, "func i32 @f(ptr %p, ptr %q) {\n"
                      "entry():\n"
                      "    %a = load i32, %p, align 4\n"
                      "    store i32 9, %q, align 4\n"
                      "    %b = load i32, %p, align 4\n"
                      "    %r = iadd i32 %a, %b\n"
                      "    ret i32 %r\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O1);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_cse(m, &cfg));
    if (m)
        T_ASSERT_EQ_INT(t, cse_count_op(m, IR_LOAD), 2);
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(log, 1, sizeof(log) - 1, report);
        log[n] = '\0';
        T_ASSERT(t, strstr(log, "load_requires_alias") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}

void test_opt_cse_does_not_merge_undef_tainted_expressions(TestCtx *t)
{
    CseFix f;
    IrModule *m;
    OptConfig cfg;

    cse_fix_init(&f);
    m = cse_parse(&f, "func i32 @f() {\n"
                      "entry():\n"
                      "    %a = iadd i32 undef, 1\n"
                      "    %b = iadd i32 undef, 1\n"
                      "    %r = xor i32 %a, %b\n"
                      "    ret i32 %r\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && !opt_cse(m, &cfg));
    if (m)
        T_ASSERT_EQ_INT(t, cse_count_op(m, IR_IADD), 2);
    arena_free_all(&f.arena);
}

void test_opt_cse_does_not_merge_ops_that_may_create_undef(TestCtx *t)
{
    CseFix f;
    IrModule *m;
    OptConfig cfg;

    cse_fix_init(&f);
    m = cse_parse(&f, "func i32 @f(i32 %x, i32 %y) {\n"
                      "entry():\n"
                      "    %a = sdiv i32 %x, %y\n"
                      "    %b = sdiv i32 %x, %y\n"
                      "    %c = shl i32 %x, %y\n"
                      "    %d = shl i32 %x, %y\n"
                      "    %r = xor i32 %a, %d\n"
                      "    ret i32 %r\n"
                      "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    T_ASSERT(t, m && !opt_cse(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, cse_count_op(m, IR_SDIV), 2);
        T_ASSERT_EQ_INT(t, cse_count_op(m, IR_SHL), 2);
    }
    arena_free_all(&f.arena);
}
