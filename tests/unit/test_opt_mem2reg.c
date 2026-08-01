#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} OFix;

static void silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void ofix_init(OFix *f)
{
    DiagSink sink = {silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *parse_ir(OFix *f, const char *src)
{
    return ir_parse_module(&f->arena, f->dc, src, "<mem2reg-test>");
}

static char *print_ir(IrModule *m, Buf *out)
{
    buf_init(out);
    ir_print_module_buf(out, m);
    buf_push_u8(out, 0);
    return (char *)out->data;
}

static u32 count_op(const IrFunc *f, IrOp op)
{
    u32 bi, n = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == op)
                n++;
    }
    return n;
}

static bool run_mem2reg(IrModule *m, OptConfig *cfg)
{
    opt_config_init(cfg, OPT_O1);
    cfg->verify_after_each = true;
    return opt_mem2reg(m, cfg);
}

static void read_report(FILE *report, char *out, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(out, 1, cap - 1, report);
    out[n] = '\0';
}

void test_mem2reg_removes_single_block_memory_traffic(TestCtx *t)
{
    OFix f;
    IrModule *m;
    OptConfig cfg;
    Buf text;
    char *s;

    ofix_init(&f);
    m = parse_ir(&f, "func i32 @f() {\n"
                     "entry():\n"
                     "    %p = alloca 4, align 4\n"
                     "    store i32 17, %p, align 4\n"
                     "    %v = load i32, %p, align 4\n"
                     "    ret i32 %v\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    T_ASSERT(t, m && run_mem2reg(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_ALLOCA), 0);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_LOAD), 0);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_STORE), 0);
        s = print_ir(m, &text);
        T_ASSERT(t, strstr(s, "ret i32 17") != NULL);
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}

void test_mem2reg_places_one_parameter_at_diamond_idf(TestCtx *t)
{
    OFix f;
    IrModule *m;
    OptConfig cfg;
    const IrInst *term;

    ofix_init(&f);
    m = parse_ir(&f, "func i32 @f(i32 %c) {\n"
                     "entry():\n"
                     "    %p = alloca 4, align 4\n"
                     "    condbr %c, left(), right()\n"
                     "left():\n"
                     "    store i32 11, %p, align 4\n"
                     "    br join()\n"
                     "right():\n"
                     "    store i32 22, %p, align 4\n"
                     "    br join()\n"
                     "join():\n"
                     "    %v = load i32, %p, align 4\n"
                     "    ret i32 %v\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    T_ASSERT(t, m && run_mem2reg(m, &cfg));
    if (m) {
        const IrFunc *fn = &m->funcs[0];

        T_ASSERT_EQ_INT(t, fn->blocks[0].nparams, 0);
        T_ASSERT_EQ_INT(t, fn->blocks[1].nparams, 0);
        T_ASSERT_EQ_INT(t, fn->blocks[2].nparams, 0);
        T_ASSERT_EQ_INT(t, fn->blocks[3].nparams, 1);
        term = fn->blocks[1].last;
        T_ASSERT_EQ_INT(t, term->edges[0].nargs, 1);
        T_ASSERT_EQ_INT(t, term->edges[0].args[0].a, 11);
        term = fn->blocks[2].last;
        T_ASSERT_EQ_INT(t, term->edges[0].nargs, 1);
        T_ASSERT_EQ_INT(t, term->edges[0].args[0].a, 22);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

static void assert_bail(TestCtx *t, const char *body, const char *reason)
{
    OFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];

    ofix_init(&f);
    m = parse_ir(&f, body);
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    if (m && report) {
        opt_config_init(&cfg, OPT_O1);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, !opt_mem2reg(m, &cfg));
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, reason) != NULL);
        T_ASSERT(t, strstr(log, "func=@f") != NULL);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_ALLOCA), 1);
    }
    if (report)
        fclose(report);
    arena_free_all(&f.arena);
}

void test_mem2reg_bails_when_alloca_address_escapes(TestCtx *t)
{
    assert_bail(t,
                "func ptr @f() {\n"
                "entry():\n"
                "    %p = alloca 8, align 8\n"
                "    store ptr %p, @sink, align 8\n"
                "    %v = load ptr, %p, align 8\n"
                "    ret ptr %v\n"
                "}\n",
                "addr_taken");
}

void test_mem2reg_bails_on_volatile_access(TestCtx *t)
{
    assert_bail(t,
                "func i32 @f() {\n"
                "entry():\n"
                "    %p = alloca 4, align 4\n"
                "    store i32 1, %p, align 4, volatile\n"
                "    %v = load i32, %p, align 4\n"
                "    ret i32 %v\n"
                "}\n",
                "volatile_access");
}

void test_mem2reg_bails_on_atomic_access(TestCtx *t)
{
    assert_bail(t,
                "func i32 @f() {\n"
                "entry():\n"
                "    %p = alloca 4, align 4\n"
                "    %v = atomicrmw xchg i32 %p, 1, seq_cst\n"
                "    ret i32 %v\n"
                "}\n",
                "volatile_access");
}

void test_mem2reg_bails_on_nonscalar_allocation(TestCtx *t)
{
    assert_bail(t,
                "func i32 @f() {\n"
                "entry():\n"
                "    %p = alloca 8, align 8\n"
                "    store i32 1, %p, align 4\n"
                "    %v = load i32, %p, align 4\n"
                "    ret i32 %v\n"
                "}\n",
                "nonscalar");
}

void test_mem2reg_bails_on_mixed_access_types(TestCtx *t)
{
    assert_bail(t,
                "func i32 @f() {\n"
                "entry():\n"
                "    %p = alloca 4, align 4\n"
                "    store i64 1, %p, align 8\n"
                "    %v = load i32, %p, align 4\n"
                "    ret i32 %v\n"
                "}\n",
                "mixed_access_type");
}

void test_mem2reg_skips_entire_setjmp_caller(TestCtx *t)
{
    OFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[512];

    ofix_init(&f);
    m = parse_ir(&f, "func i32 @f(ptr %b) setjmp {\n"
                     "entry():\n"
                     "    %p = alloca 4, align 4\n"
                     "    store i32 9, %p, align 4\n"
                     "    %r = call i32 @setjmp(ptr %b)\n"
                     "    %v = load i32, %p, align 4\n"
                     "    ret i32 %v\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    if (m && report) {
        opt_config_init(&cfg, OPT_O1);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, !opt_mem2reg(m, &cfg));
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "setjmp_caller") != NULL);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_ALLOCA), 1);
    }
    if (report)
        fclose(report);
    arena_free_all(&f.arena);
}

void test_mem2reg_undef_log_survives_renumber_with_block_and_span(TestCtx *t)
{
    OFix f;
    IrModule *m;
    IrFunc *fn;
    IrBuilder b;
    BlockId entry;
    ValueId ptr, value;
    IrOperand ret;
    OptConfig cfg;
    Span loc = {0};
    const UndefUse *uses;
    u32 nuses = 0;
    static const char src[] = "return x;\n";

    ofix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "f", IRT_I32, NULL, 0);
    entry = ir_block_new(m, fn, "entry");
    ir_builder_at(&b, m, fn, entry);
    ptr = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 4), 4);
    loc.file_id = diag_add_file(f.dc, "undef.c", src, sizeof(src) - 1);
    loc.line = 1;
    loc.col = 8;
    loc.len = 1;
    loc.presumed_path = "logical-undef.c";
    loc.presumed_line = 71;
    ir_builder_set_span(&b, loc);
    value = ir_build_load(&b, IRT_I32, ir_op_value(fn, ptr), 4, 0);
    ret = ir_op_value(fn, value);
    ir_build_ret(&b, &ret);

    T_ASSERT(t, ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O1);
    cfg.verify_after_each = true;
    T_ASSERT(t, opt_run_pipeline(m, &cfg));
    uses = opt_mem2reg_undef_log(fn, &nuses);
    T_ASSERT_EQ_INT(t, nuses, 1);
    T_ASSERT(t, uses != NULL);
    if (uses) {
        T_ASSERT_EQ_INT(t, uses[0].alloca_ord, 0);
        T_ASSERT_EQ_INT(t, uses[0].block.v, entry.v);
        T_ASSERT_EQ_INT(t, uses[0].loc.file_id, loc.file_id);
        T_ASSERT_EQ_INT(t, uses[0].loc.line, 1);
        T_ASSERT_EQ_INT(t, uses[0].loc.col, 8);
        T_ASSERT_EQ_INT(t, uses[0].loc.presumed_line, 71);
        T_ASSERT_EQ_STR(t, uses[0].loc.presumed_path, "logical-undef.c");
    }
    T_ASSERT(t, ir_verify(f.dc, m));
    arena_free_all(&f.arena);
}
