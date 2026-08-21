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

void test_mem2reg_symbolic_indirect_call_roundtrips(TestCtx *t)
{
    OFix f;
    IrModule *m, *round;
    OptConfig cfg;
    Buf text;
    char *printed;
    const IrInst *call;

    ofix_init(&f);
    m = parse_ir(&f, "func i32 @target(i32 %x) {\n"
                     "entry():\n"
                     "    ret i32 %x\n"
                     "}\n"
                     "func i32 @caller(i32 %x) {\n"
                     "entry():\n"
                     "    %slot = alloca 8, align 8\n"
                     "    store ptr @target, %slot, align 8\n"
                     "    %fp = load ptr, %slot, align 8\n"
                     "    %r = call i32 %fp(i32 %x)\n"
                     "    ret i32 %r\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    T_ASSERT(t, m && run_mem2reg(m, &cfg));
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    call = m->funcs[1].blocks[0].first;
    T_ASSERT(t, call != NULL && call->op == IR_CALL);
    if (call) {
        T_ASSERT_EQ_INT(t, call->subop, FUNCREF_INDIRECT);
        T_ASSERT_EQ_INT(t, call->ops[0].kind, IROP_SYMBOL);
    }

    printed = print_ir(m, &text);
    T_ASSERT(t, strstr(printed, "call i32 indirect @target(i32 %0)") != NULL);
    round = ir_parse_module(&f.arena, f.dc, printed, "<mem2reg-roundtrip>");
    T_ASSERT(t, round != NULL);
    T_ASSERT(t, round && ir_verify(f.dc, round));
    T_ASSERT(t, round && ir_module_struct_eq(m, round));
    buf_free(&text);
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

void test_mem2reg_keeps_f80_in_memory(TestCtx *t)
{
    assert_bail(t,
                "func f80 @f() {\n"
                "entry():\n"
                "    %p = alloca 16, align 16\n"
                "    store f80 0x3FFF:0x8000000000000000, %p, align 16\n"
                "    %v = load f80, %p, align 16\n"
                "    ret f80 %v\n"
                "}\n",
                "memory_only_type");
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

void test_mem2reg_undef_log_distinguishes_definite_maybe_and_self(TestCtx *t)
{
    OFix f;
    IrModule *m;
    OptConfig cfg;
    const UndefUse *uses;
    u32 nuses = 0;

    ofix_init(&f);
    m = parse_ir(&f, "func i32 @f(i32 %c) {\n"
                     "entry():\n"
                     "    %p = alloca 4, align 4\n"
                     "    condbr %c, defined(), join()\n"
                     "defined():\n"
                     "    store i32 1, %p, align 4\n"
                     "    br join()\n"
                     "join():\n"
                     "    %v = load i32, %p, align 4\n"
                     "    ret i32 %v\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    T_ASSERT(t, m && run_mem2reg(m, &cfg));
    if (m) {
        uses = opt_mem2reg_undef_log(&m->funcs[0], &nuses);
        T_ASSERT_EQ_INT(t, nuses, 1);
        if (uses) {
            T_ASSERT_EQ_INT(t, uses[0].classification, UNDEF_USE_MAYBE);
            T_ASSERT_EQ_INT(t, uses[0].decision_kind, 2);
            T_ASSERT(t, !uses[0].path_undecided);
        }
    }
    arena_free_all(&f.arena);

    ofix_init(&f);
    m = parse_ir(&f, "func i32 @g() {\n"
                     "entry():\n"
                     "    %x = alloca 4, align 4\n"
                     "    %y = alloca 4, align 4\n"
                     "    %u = load i32, %y, align 4, self_init\n"
                     "    store i32 %u, %x, align 4\n"
                     "    %v = load i32, %x, align 4\n"
                     "    ret i32 %v\n"
                     "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    T_ASSERT(t, m && run_mem2reg(m, &cfg));
    if (m) {
        uses = opt_mem2reg_undef_log(&m->funcs[0], &nuses);
        T_ASSERT_EQ_INT(t, nuses, 1);
        if (uses) {
            T_ASSERT_EQ_INT(t, uses[0].classification, UNDEF_USE_DEFINITE);
            T_ASSERT(t, uses[0].self_init);
        }
    }
    arena_free_all(&f.arena);
}

void test_mem2reg_definite_assignment_worklist_handles_reverse_chain(TestCtx *t)
{
    enum { CHAIN_BLOCKS = 256 };
    OFix f;
    IrModule *m;
    IrFunc *fn;
    IrBuilder b;
    BlockId entry;
    BlockId *chain;
    ValueId ptr;
    ValueId value;
    IrOperand ret;
    OptConfig cfg;
    const UndefUse *uses;
    u32 nuses = 0;
    u32 i;

    ofix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "reverse_chain", IRT_I32, NULL, 0);
    entry = ir_block_new(m, fn, "entry");
    chain =
        arena_alloc(&f.arena, sizeof(*chain) * CHAIN_BLOCKS, _Alignof(BlockId));
    for (i = 0; i < CHAIN_BLOCKS; i++)
        chain[i] = ir_block_new(m, fn, "chain");

    ir_builder_at(&b, m, fn, entry);
    ptr = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 4), 4);
    ir_build_br(&b, chain[CHAIN_BLOCKS - 1], NULL, 0);
    for (i = CHAIN_BLOCKS - 1; i > 0; i--) {
        ir_builder_at(&b, m, fn, chain[i]);
        ir_build_br(&b, chain[i - 1], NULL, 0);
    }
    ir_builder_at(&b, m, fn, chain[0]);
    value = ir_build_load(&b, IRT_I32, ir_op_value(fn, ptr), 4, 0);
    ret = ir_op_value(fn, value);
    ir_build_ret(&b, &ret);

    T_ASSERT(t, ir_verify(f.dc, m));
    T_ASSERT(t, run_mem2reg(m, &cfg));
    uses = opt_mem2reg_undef_log(fn, &nuses);
    T_ASSERT_EQ_INT(t, nuses, 1);
    if (uses)
        T_ASSERT_EQ_INT(t, uses[0].classification, UNDEF_USE_DEFINITE);
    T_ASSERT(t, ir_verify(f.dc, m));
    arena_free_all(&f.arena);
}

void test_mem2reg_flow_witness_scales_across_many_guarded_uses(TestCtx *t)
{
    enum { STRESS_USES = 256 };
    OFix f;
    IrModule *m;
    IrFunc *fn;
    IrBuilder b;
    IrType params[2] = {IRT_I32, IRT_I32};
    BlockId entry, defined;
    BlockId *guards, *uses, *next;
    ValueId value_slot, guard_slot;
    OptConfig cfg;
    const UndefUse *undefs;
    u32 nundefs = 0;
    u32 i;

    ofix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "many_guards", IRT_I32, params, 2);
    entry = ir_block_new(m, fn, "entry");
    defined = ir_block_new(m, fn, "defined");
    guards =
        arena_alloc(&f.arena, sizeof(*guards) * STRESS_USES, _Alignof(BlockId));
    uses =
        arena_alloc(&f.arena, sizeof(*uses) * STRESS_USES, _Alignof(BlockId));
    next =
        arena_alloc(&f.arena, sizeof(*next) * STRESS_USES, _Alignof(BlockId));
    for (i = 0; i < STRESS_USES; i++) {
        guards[i] = ir_block_new(m, fn, "guard");
        uses[i] = ir_block_new(m, fn, "use");
        next[i] = ir_block_new(m, fn, "next");
    }

    ir_builder_at(&b, m, fn, entry);
    value_slot = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 4), 4);
    guard_slot = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 4), 4);
    ir_build_store(&b, ir_op_value(fn, fn->param_vals[1]),
                   ir_op_value(fn, guard_slot), 4, 0);
    ir_build_condbr(&b, ir_op_value(fn, fn->param_vals[0]), defined, NULL, 0,
                    guards[0], NULL, 0);

    ir_builder_at(&b, m, fn, defined);
    ir_build_store(&b, ir_op_iconst(IRT_I32, 1), ir_op_value(fn, value_slot), 4,
                   0);
    ir_build_br(&b, guards[0], NULL, 0);

    for (i = 0; i < STRESS_USES; i++) {
        ValueId condition;

        ir_builder_at(&b, m, fn, guards[i]);
        condition =
            ir_build_load(&b, IRT_I32, ir_op_value(fn, guard_slot), 4, 0);
        ir_build_condbr(&b, ir_op_value(fn, condition), uses[i], NULL, 0,
                        next[i], NULL, 0);

        ir_builder_at(&b, m, fn, uses[i]);
        (void)ir_build_load(&b, IRT_I32, ir_op_value(fn, value_slot), 4, 0);
        ir_build_br(&b, next[i], NULL, 0);

        ir_builder_at(&b, m, fn, next[i]);
        if (i + 1 < STRESS_USES)
            ir_build_br(&b, guards[i + 1], NULL, 0);
        else {
            IrOperand zero = ir_op_iconst(IRT_I32, 0);

            ir_build_ret(&b, &zero);
        }
    }

    T_ASSERT(t, ir_verify(f.dc, m));
    T_ASSERT(t, run_mem2reg(m, &cfg));
    undefs = opt_mem2reg_undef_log(fn, &nundefs);
    T_ASSERT_EQ_INT(t, nundefs, STRESS_USES);
    if (undefs)
        T_ASSERT_EQ_INT(t, undefs[STRESS_USES - 1].classification,
                        UNDEF_USE_MAYBE);
    T_ASSERT(t, ir_verify(f.dc, m));
    arena_free_all(&f.arena);
}
