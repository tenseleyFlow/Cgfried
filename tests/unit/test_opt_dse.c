#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} DseFix;

bool opt_dse(IrModule *m, const OptConfig *cfg);

static void silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void fix_init(DseFix *f)
{
    DiagSink sink = {silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *parse(DseFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<dse-test>");
}

static u32 count_op(const IrModule *m, IrOp op)
{
    const IrFunc *fn = &m->funcs[0];
    const IrInst *in;
    u32 bi, n = 0;

    for (bi = 0; bi < fn->nblocks; bi++)
        for (in = fn->blocks[bi].first; in; in = in->next)
            n += in->op == op;
    return n;
}

static bool run(IrModule *m, OptConfig *cfg)
{
    opt_config_init(cfg, OPT_O2);
    cfg->verify_after_each = true;
    return opt_dse(m, cfg);
}

void test_opt_dse_exact_overwrite_dies_but_adjacent_and_may_stay(TestCtx *t)
{
    DseFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func void @f(ptr %p, ptr %q) {\n"
                  "entry():\n"
                  "    store i32 1, %p, align 4\n"
                  "    store i32 2, %p, align 4\n"
                  "    %p4 = ptradd %p, 4\n"
                  "    store i32 3, %p4, align 4\n"
                  "    store i32 4, %q, align 4\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && run(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_STORE), 3);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_dse_combines_adjacent_field_stores_to_cover_memset(TestCtx *t)
{
    DseFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func ptr @f() {\n"
                  "entry():\n"
                  "    %p = alloca 16, align 8\n"
                  "    memset %p, 0, 16, align 1\n"
                  "    store i32 1, %p, align 4\n"
                  "    %p4 = ptradd %p, 4\n"
                  "    store i32 2, %p4, align 4\n"
                  "    %p8 = ptradd %p, 8\n"
                  "    store i32 3, %p8, align 4\n"
                  "    %p12 = ptradd %p, 12\n"
                  "    store i32 4, %p12, align 4\n"
                  "    ret ptr %p\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && run(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_MEMSET), 0);
        T_ASSERT_EQ_INT(t, count_op(m, IR_STORE), 4);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_dse_partial_overlapping_store_does_not_kill(TestCtx *t)
{
    DseFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func void @f(ptr %p) {\n"
                  "entry():\n"
                  "    store i64 1, %p, align 8\n"
                  "    %p4 = ptradd %p, 4\n"
                  "    store i32 2, %p4, align 4\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && !run(m, &cfg));
    if (m)
        T_ASSERT_EQ_INT(t, count_op(m, IR_STORE), 2);
    arena_free_all(&f.arena);
}

void test_opt_dse_partial_memset_coverage_stays_and_logs(TestCtx *t)
{
    DseFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[256];
    size_t n;

    fix_init(&f);
    m = parse(&f, "func ptr @f() {\n"
                  "entry():\n"
                  "    %p = alloca 16, align 8\n"
                  "    memset %p, 0, 16, align 1\n"
                  "    store i32 1, %p, align 4\n"
                  "    %p4 = ptradd %p, 4\n"
                  "    store i32 2, %p4, align 4\n"
                  "    ret ptr %p\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_dse(m, &cfg));
    if (m)
        T_ASSERT_EQ_INT(t, count_op(m, IR_MEMSET), 1);
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(log, 1, sizeof(log) - 1, report);
        log[n] = '\0';
        T_ASSERT(t, strstr(log, "dse_partial_overwrite") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}

void test_opt_dse_load_and_call_are_barriers(TestCtx *t)
{
    DseFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[256];
    size_t n;

    fix_init(&f);
    m = parse(&f, "func i32 @f(ptr %p) {\n"
                  "entry():\n"
                  "    store i32 1, %p, align 4\n"
                  "    %v = load i32, %p, align 4\n"
                  "    store i32 2, %p, align 4\n"
                  "    call void @side(ptr %p)\n"
                  "    store i32 3, %p, align 4\n"
                  "    ret i32 %v\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_dse(m, &cfg));
    if (m)
        T_ASSERT_EQ_INT(t, count_op(m, IR_STORE), 3);
    if (report) {
        fflush(report);
        rewind(report);
        n = fread(log, 1, sizeof(log) - 1, report);
        log[n] = '\0';
        T_ASSERT(t, strstr(log, "dse_call_barrier") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}

void test_opt_dse_unknown_width_load_is_a_barrier(TestCtx *t)
{
    DseFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func f80 @f() {\n"
                  "entry():\n"
                  "    %u = alloca 16, align 16, etype union\n"
                  "    %se = ptradd %u, 8\n"
                  "    store i16 16382, %se, align 2, etype union\n"
                  "    %x = load f80, %u, align 16, etype union\n"
                  "    ret f80 %x\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && !run(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_STORE), 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_dse_final_nonescaping_store_dies_but_escaped_stays(TestCtx *t)
{
    DseFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func void @local() {\n"
                  "entry():\n"
                  "    %p = alloca 4, align 4\n"
                  "    store i32 1, %p, align 4\n"
                  "    ret\n"
                  "}\n"
                  "func ptr @escaped() {\n"
                  "entry():\n"
                  "    %p = alloca 4, align 4\n"
                  "    store i32 2, %p, align 4\n"
                  "    ret ptr %p\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && run(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_STORE), 0);
        T_ASSERT_EQ_INT(t, m->funcs[1].blocks[0].ninsts, 3);
        T_ASSERT_EQ_INT(t, m->funcs[1].blocks[0].first->next->op, IR_STORE);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_dse_volatile_and_atomic_memory_survive(TestCtx *t)
{
    DseFix f;
    IrModule *m;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func void @f(ptr %p) {\n"
                  "entry():\n"
                  "    store i32 1, %p, align 4, volatile\n"
                  "    store i32 2, %p, align 4, seq_cst\n"
                  "    %a = atomicrmw add i32 %p, 1, seq_cst\n"
                  "    %c = cmpxchg i32 %p, 1, 2, seq_cst\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && !run(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(m, IR_STORE), 2);
        T_ASSERT_EQ_INT(t, count_op(m, IR_ATOMICRMW), 1);
        T_ASSERT_EQ_INT(t, count_op(m, IR_CMPXCHG), 1);
    }
    arena_free_all(&f.arena);
}
