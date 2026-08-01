#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} LicmFix;

static void silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void fix_init(LicmFix *f)
{
    DiagSink sink = {silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *parse(LicmFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<licm-test>");
}

static IrInst *find_op(IrFunc *f, IrOp op, u32 ordinal)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == op && ordinal-- == 0)
                return in;
    }
    return NULL;
}

static BlockId find_inst_block(IrFunc *f, const IrInst *wanted)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in == wanted)
                return (BlockId){bi + 1};
    }
    return BLOCK_INVALID;
}

static bool run(IrModule *m, OptConfig *cfg, FILE *report)
{
    opt_config_init(cfg, OPT_O2);
    cfg->verify_after_each = true;
    cfg->bail_log = report != NULL;
    cfg->report = report;
    return opt_licm(m, cfg);
}

static void read_report(FILE *report, char *out, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(out, 1, cap - 1, report);
    out[n] = '\0';
}

void test_opt_licm_hoists_invariant_pure_expression(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    IrInst *add;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func i32 @f(i32 %c, i32 %x, i32 %y) {\n"
                  "entry():\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    %sum = iadd i32 %x, %y\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret i32 %x\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && run(m, &cfg, NULL));
    if (m) {
        add = find_op(&m->funcs[0], IR_IADD, 0);
        T_ASSERT(t, add != NULL);
        if (add)
            T_ASSERT_EQ_INT(t, m->funcs[0].vals[add->result.v - 1].def_block.v,
                            1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_licm_division_requires_full_safety_proof(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    IrInst *div;
    OptConfig cfg;
    FILE *report = tmpfile();
    char log[512];

    fix_init(&f);
    m = parse(&f, "func i32 @f(i32 %c, i32 %x, i32 %d) {\n"
                  "entry():\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    %q = sdiv i32 %x, %d\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret i32 %x\n"
                  "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(f.dc, m));
    if (m)
        (void)run(m, &cfg, report);
    if (m) {
        div = find_op(&m->funcs[0], IR_SDIV, 0);
        T_ASSERT(t, div != NULL);
        if (div)
            T_ASSERT_EQ_INT(t, m->funcs[0].vals[div->result.v - 1].def_block.v,
                            3);
    }
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "licm_div_not_nonzero") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}

void test_opt_licm_division_hoists_only_when_overflow_safe(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    IrInst *safe;
    IrInst *danger;
    OptConfig cfg;
    FILE *report = tmpfile();
    char log[512];

    fix_init(&f);
    m = parse(&f, "func i32 @safe(i32 %c, i32 %x) {\n"
                  "entry():\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    %q = udiv i32 %x, 2\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret i32 %x\n"
                  "}\n"
                  "func i32 @danger(i32 %c, i32 %x) {\n"
                  "entry():\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    %q = sdiv i32 %x, 4294967295\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret i32 %x\n"
                  "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(f.dc, m));
    if (m)
        (void)run(m, &cfg, report);
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "licm_div_not_nonzero") != NULL);
        fclose(report);
    }
    if (m) {
        safe = find_op(&m->funcs[0], IR_UDIV, 0);
        danger = find_op(&m->funcs[1], IR_SDIV, 0);
        T_ASSERT(t, safe != NULL && danger != NULL);
        if (safe)
            T_ASSERT_EQ_INT(t, find_inst_block(&m->funcs[0], safe).v, 1);
        if (danger)
            T_ASSERT(t, find_inst_block(&m->funcs[1], danger).v != 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_licm_hoists_dereferenceable_unclobbered_load(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    IrInst *load;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func i32 @f(i32 %c) {\n"
                  "entry():\n"
                  "    %p = alloca 4, align 4\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    %v = load i32, %p, align 4, etype i32\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret i32 0\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && run(m, &cfg, NULL));
    if (m) {
        load = find_op(&m->funcs[0], IR_LOAD, 0);
        T_ASSERT(t, load != NULL);
        if (load)
            T_ASSERT_EQ_INT(t, m->funcs[0].vals[load->result.v - 1].def_block.v,
                            1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_licm_load_bails_for_guard_or_aliasing_write(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report = tmpfile();
    char log[1024];

    fix_init(&f);
    m = parse(&f, "func i32 @f(i32 %c, ptr %p) {\n"
                  "entry():\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    %v = load i32, %p, align 4, etype i32\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret i32 0\n"
                  "}\n"
                  "func i32 @g(i32 %c) {\n"
                  "entry():\n"
                  "    %p = alloca 4, align 4\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    %v = load i32, %p, align 4, etype i32\n"
                  "    store i32 1, %p, align 4, etype i32\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret i32 0\n"
                  "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(f.dc, m));
    if (m)
        (void)run(m, &cfg, report);
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "licm_load_not_guaranteed") != NULL);
        T_ASSERT(t, strstr(log, "licm_load_clobbered") != NULL);
        T_ASSERT(t, strstr(log, "licm_sink_unsafe") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}

void test_opt_licm_load_does_not_cross_infinite_bypass(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    IrInst *load;
    OptConfig cfg;
    FILE *report = tmpfile();
    char log[512];

    fix_init(&f);
    m = parse(&f, "func i32 @f(i32 %spin, i32 %leave, ptr %p) {\n"
                  "entry():\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %spin, bypass(), loadblock()\n"
                  "bypass():\n"
                  "    br head()\n"
                  "loadblock():\n"
                  "    %v = load i32, %p, align 4, etype i32\n"
                  "    condbr %leave, exit(i32 %v), latch()\n"
                  "latch():\n"
                  "    br head()\n"
                  "exit(i32 %r):\n"
                  "    ret i32 %r\n"
                  "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(f.dc, m));
    if (m)
        (void)run(m, &cfg, report);
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "licm_load_not_guaranteed") != NULL);
        fclose(report);
    }
    if (m) {
        load = find_op(&m->funcs[0], IR_LOAD, 0);
        T_ASSERT(t, load != NULL);
        if (load)
            T_ASSERT(t, find_inst_block(&m->funcs[0], load).v != 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_licm_memory_does_not_cross_va_start(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    IrInst *load;
    IrInst *store;
    OptConfig cfg;
    FILE *report = tmpfile();
    char log[1024];

    fix_init(&f);
    m = parse(&f, "func i32 @load_barrier(i32 %c, ...) {\n"
                  "entry():\n"
                  "    %p = alloca 24, align 8\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    va_start %p\n"
                  "    %v = load i64, %p, align 8, etype i64\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret i32 0\n"
                  "}\n"
                  "func void @store_barrier(i32 %c, ptr %p, i64 %v, ...) {\n"
                  "entry():\n"
                  "    br head()\n"
                  "head():\n"
                  "    store i64 %v, %p, align 8, etype i64\n"
                  "    va_start %p\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(f.dc, m));
    if (m)
        (void)run(m, &cfg, report);
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "licm_load_clobbered") != NULL);
        T_ASSERT(t, strstr(log, "licm_sink_unsafe") != NULL);
        fclose(report);
    }
    if (m) {
        load = find_op(&m->funcs[0], IR_LOAD, 0);
        store = find_op(&m->funcs[1], IR_STORE, 0);
        T_ASSERT(t, load != NULL && store != NULL);
        if (load)
            T_ASSERT(t, find_inst_block(&m->funcs[0], load).v != 1);
        if (store)
            T_ASSERT(t, find_inst_block(&m->funcs[1], store).v != 4);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_licm_sinks_single_exit_unconditionally_executed_store(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    IrInst *store;
    OptConfig cfg;

    fix_init(&f);
    m = parse(&f, "func void @f(i32 %c, ptr %p, i32 %v) {\n"
                  "entry():\n"
                  "    br head()\n"
                  "head():\n"
                  "    store i32 %v, %p, align 4, etype i32\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    T_ASSERT(t, m && run(m, &cfg, NULL));
    if (m) {
        store = find_op(&m->funcs[0], IR_STORE, 0);
        T_ASSERT(t, store != NULL);
        if (store)
            T_ASSERT_EQ_INT(t, find_inst_block(&m->funcs[0], store).v, 4);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

void test_opt_licm_never_moves_volatile_or_call(TestCtx *t)
{
    LicmFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report = tmpfile();
    char log[1024];

    fix_init(&f);
    m = parse(&f, "sym @side\n"
                  "func void @f(i32 %c) {\n"
                  "entry():\n"
                  "    %p = alloca 4, align 4\n"
                  "    br head()\n"
                  "head():\n"
                  "    condbr %c, body(), exit()\n"
                  "body():\n"
                  "    %v = load i32, %p, align 4, volatile, etype i32\n"
                  "    call void @side()\n"
                  "    br head()\n"
                  "exit():\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, report != NULL && m && ir_verify(f.dc, m));
    if (m)
        (void)run(m, &cfg, report);
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "licm_volatile") != NULL);
        T_ASSERT(t, strstr(log, "licm_call") != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}
