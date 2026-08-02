#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

extern const Pass OPT_PASS_INLINE;
bool opt_inline(IrModule *m, const OptConfig *cfg);

typedef struct {
    Arena arena;
    DiagCtx *dc;
} InlineFix;

static void inline_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void inline_fix_init(InlineFix *f)
{
    DiagSink sink = {inline_silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *inline_parse(InlineFix *f, const char *source)
{
    return ir_parse_module(&f->arena, f->dc, source, "<inline-test>");
}

static char *inline_print(IrModule *m, Buf *out)
{
    buf_init(out);
    ir_print_module_buf(out, m);
    buf_push_u8(out, 0);
    return (char *)out->data;
}

static u32 func_op_count(const IrFunc *f, IrOp op)
{
    u32 bi, n = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            n += in->op == op;
    }
    return n;
}

static IrInst *first_op(IrFunc *f, IrOp op)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == op)
                return in;
    }
    return NULL;
}

static IrInst *store_of_iconst(IrFunc *f, i64 value)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == IR_STORE && in->nops &&
                in->ops[0].kind == IROP_ICONST && (i64)in->ops[0].a == value)
                return in;
    }
    return NULL;
}

static void read_report(FILE *report, char *out, size_t cap)
{
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(out, 1, cap - 1, report);
    out[n] = '\0';
}

void test_opt_inline_single_return_metadata_and_repeatability(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    Buf first, second;
    IrFunc *caller;
    IrInst *store, *rmw;
    const Pass *passes[] = {&OPT_PASS_INLINE};
    char *one;
    const char *caller_before;
    const char *caller_text;
    const char *before;
    const char *after;
    const char *caller_after;

    inline_fix_init(&f);
    m = inline_parse(&f, "sym @before\n"
                         "sym @after\n"
                         "func i32 @touch(ptr %p) {\n"
                         "entry():\n"
                         "    call void @before()\n"
                         "    store i32 7, %p, align 4, volatile, etype union\n"
                         "    %old = atomicrmw add i32 %p, 1, seq_cst\n"
                         "    call void @after()\n"
                         "    ret i32 %old\n"
                         "}\n"
                         "func i32 @caller(ptr %p) {\n"
                         "entry():\n"
                         "    store i32 1, %p, align 4, volatile, etype i32\n"
                         "    %r = call i32 @touch(ptr %p)\n"
                         "    store i32 9, %p, align 4, volatile, etype i32\n"
                         "    %z = iadd i32 %r, 2\n"
                         "    ret i32 %z\n"
                         "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_run_pass_sequence(m, &cfg, passes, 1));
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    caller = &m->funcs[1];
    T_ASSERT_EQ_INT(t, func_op_count(caller, IR_CALL), 2);
    T_ASSERT_EQ_INT(t, func_op_count(caller, IR_STORE), 3);
    T_ASSERT_EQ_INT(t, func_op_count(caller, IR_ATOMICRMW), 1);
    store = store_of_iconst(caller, 7);
    rmw = first_op(caller, IR_ATOMICRMW);
    T_ASSERT(t, store && store->align == 4 && store->subop == ETYPE_UNION &&
                    (store->flags & IRF_VOLATILE));
    T_ASSERT(t, rmw && rmw->subop == RMW_ADD && (rmw->flags & IRF_SEQ_CST));
    T_ASSERT(t, ir_verify(f.dc, m));
    one = inline_print(m, &first);
    caller_text = strstr(one, "func i32 @caller");
    caller_before = caller_text ? strstr(caller_text, "store i32 1") : NULL;
    before = caller_text ? strstr(caller_text, "call void @before()") : NULL;
    after = caller_text ? strstr(caller_text, "call void @after()") : NULL;
    caller_after = caller_text ? strstr(caller_text, "store i32 9") : NULL;
    T_ASSERT(t, caller_before && before && after && caller_after &&
                    caller_before < before && before < after &&
                    after < caller_after);
    T_ASSERT(t, strstr(one, "inl.0.join") == NULL);
    T_ASSERT(t, !opt_inline(m, &cfg));
    (void)inline_print(m, &second);
    T_ASSERT_EQ_INT(t, first.len, second.len);
    T_ASSERT(t, first.len == second.len &&
                    memcmp(first.data, second.data, first.len) == 0);
    buf_free(&first);
    buf_free(&second);
    arena_free_all(&f.arena);
}

void test_opt_inline_multi_return_uses_one_join_parameter(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    IrFunc *caller;
    IrBlock *join = NULL;
    const Pass *passes[] = {&OPT_PASS_INLINE};
    u32 i;

    inline_fix_init(&f);
    m = inline_parse(&f, "func i32 @choose(i32 %c, i32 %x) {\n"
                         "entry():\n"
                         "    condbr %c, yes(), no()\n"
                         "yes():\n"
                         "    ret i32 %x\n"
                         "no():\n"
                         "    ret i32 9\n"
                         "}\n"
                         "func i32 @caller(i32 %c, i32 %x) {\n"
                         "entry():\n"
                         "    %r = call i32 @choose(i32 %c, i32 %x)\n"
                         "    %z = imul i32 %r, 3\n"
                         "    ret i32 %z\n"
                         "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_run_pass_sequence(m, &cfg, passes, 1));
    if (m) {
        caller = &m->funcs[1];
        for (i = 0; i < caller->nblocks; i++)
            if (caller->blocks[i].name &&
                strcmp(caller->blocks[i].name, "inl.0.join") == 0)
                join = &caller->blocks[i];
        T_ASSERT(t, join != NULL);
        T_ASSERT_EQ_INT(t, join ? join->nparams : 0, 1);
        T_ASSERT_EQ_INT(t, func_op_count(caller, IR_CALL), 0);
        T_ASSERT_EQ_INT(t, func_op_count(caller, IR_RET), 1);
        T_ASSERT(t, ir_verify(f.dc, m));
    }
    arena_free_all(&f.arena);
}

static void assert_bail(TestCtx *t, const char *source, const char *reason,
                        bool patch_mutual)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char log[2048];
    u32 before = 0, after = 0;

    inline_fix_init(&f);
    m = inline_parse(&f, source);
    if (m && patch_mutual) {
        IrInst *call = first_op(&m->funcs[0], IR_CALL);

        if (call) {
            call->subop = FUNCREF_INTERNAL;
            call->callee = 1;
        }
    }
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    for (u32 i = 0; i < m->nfuncs; i++)
        before += func_op_count(&m->funcs[i], IR_CALL);
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 1000;
    cfg.debug_info = strcmp(reason, "inl_debug_info") == 0;
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, !opt_inline(m, &cfg));
    for (u32 i = 0; i < m->nfuncs; i++)
        after += func_op_count(&m->funcs[i], IR_CALL);
    T_ASSERT_EQ_INT(t, before, after);
    T_ASSERT(t, ir_verify(f.dc, m));
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, reason) != NULL);
        fclose(report);
    }
    arena_free_all(&f.arena);
}

void test_opt_inline_never_inline_bail_matrix(TestCtx *t)
{
    assert_bail(t,
                "func i32 @old(i32 %x) unproto {\n"
                "entry():\n"
                "    ret i32 %x\n"
                "}\n"
                "func i32 @caller() {\n"
                "entry():\n"
                "    %x = call i32 @old()\n"
                "    ret i32 %x\n"
                "}\n",
                "inl_unprototyped_signature", false);
    assert_bail(t,
                "func void @v(ptr %p, ...) {\n"
                "entry():\n"
                "    va_start %p\n"
                "    ret\n"
                "}\n"
                "func void @caller(ptr %p) {\n"
                "entry():\n"
                "    call void @v(ptr %p) va\n"
                "    ret\n"
                "}\n",
                "inl_va_start", false);
    assert_bail(t,
                "sym @setjmp\n"
                "func void @target() {\nentry():\n    ret\n}\n"
                "func i32 @caller(ptr %b) setjmp {\n"
                "entry():\n"
                "    %x = call i32 @setjmp(ptr %b)\n"
                "    call void @target()\n"
                "    ret i32 %x\n"
                "}\n",
                "inl_setjmp", false);
    assert_bail(t,
                "sym @setjmp\n"
                "func i32 @target(ptr %b) setjmp {\n"
                "entry():\n"
                "    %x = call i32 @setjmp(ptr %b)\n"
                "    ret i32 %x\n"
                "}\n"
                "func i32 @caller(ptr %b) {\n"
                "entry():\n"
                "    %x = call i32 @target(ptr %b)\n"
                "    ret i32 %x\n"
                "}\n",
                "inl_setjmp", false);
    assert_bail(t,
                "func void @stop() {\n"
                "entry():\n"
                "    unreachable\n"
                "}\n"
                "func void @caller() {\n"
                "entry():\n"
                "    call void @stop()\n"
                "    ret\n"
                "}\n",
                "inl_noreturn", false);
    assert_bail(t,
                "func void @stacky() {\n"
                "entry():\n"
                "    %p = alloca 8, align 8\n"
                "    ret\n"
                "}\n"
                "func void @caller(i32 %c) {\n"
                "entry():\n"
                "    br loop()\n"
                "loop():\n"
                "    call void @stacky()\n"
                "    condbr %c, loop(), exit()\n"
                "exit():\n"
                "    ret\n"
                "}\n",
                "inl_alloca_in_loop", false);
    /* Neither entry into this cyclic SCC dominates the other. */
    assert_bail(t,
                "func void @stacky() {\n"
                "entry():\n"
                "    %p = alloca 8, align 8\n"
                "    ret\n"
                "}\n"
                "func void @caller(i32 %a, i32 %b) {\n"
                "entry():\n"
                "    condbr %a, left(), right()\n"
                "left():\n"
                "    call void @stacky()\n"
                "    condbr %b, right(), exit()\n"
                "right():\n"
                "    condbr %b, left(), exit()\n"
                "exit():\n"
                "    ret\n"
                "}\n",
                "inl_alloca_in_loop", false);
    assert_bail(t,
                "func i32 @a(i32 %x) {\n"
                "entry():\n"
                "    %r = call i32 @b(i32 %x)\n"
                "    ret i32 %r\n"
                "}\n"
                "func i32 @b(i32 %x) {\n"
                "entry():\n"
                "    %r = call i32 @a(i32 %x)\n"
                "    ret i32 %r\n"
                "}\n"
                "func i32 @outside(i32 %x) {\n"
                "entry():\n"
                "    %r = call i32 @a(i32 %x)\n"
                "    ret i32 %r\n"
                "}\n",
                "inl_recursion", true);
    assert_bail(t,
                "func i32 @caller(ptr %fp, i32 %x) {\n"
                "entry():\n"
                "    %r = call i32 %fp(i32 %x)\n"
                "    ret i32 %r\n"
                "}\n",
                "inl_indirect", false);
    assert_bail(t,
                "func i32 @target(i32 %x) {\n"
                "entry():\n"
                "    ret i32 %x\n"
                "}\n"
                "func i32 @caller(i32 %x) {\n"
                "entry():\n"
                "    %r = call i32 @target(i32 %x)\n"
                "    ret i32 %r\n"
                "}\n",
                "inl_debug_info", false);
}

static void emit_cost_function(Buf *src, const char *name, u32 nadds,
                               bool internal)
{
    u32 i;

    buf_printf(src, "func i32 @%s(i32 %%c, i32 %%x)%s {\nentry():\n", name,
               internal ? " internal" : "");
    for (i = 0; i < nadds; i++)
        buf_printf(src, "    %%v%u = iadd i32 %%x, %u\n", i, i + 1);
    buf_printf(src, "    condbr %%c, yes(), no()\nyes():\n"
                    "    ret i32 %%x\nno():\n    ret i32 0\n}\n");
}

static IrModule *cost_module(InlineFix *f, bool internal)
{
    Buf src;
    IrModule *m;

    buf_init(&src);
    emit_cost_function(&src, "large", 23, internal);
    buf_printf(&src, "func i32 @caller() {\nentry():\n"
                     "    %%r = call i32 @large(i32 1, i32 7)\n"
                     "    ret i32 %%r\n}\n");
    buf_push_u8(&src, 0);
    m = inline_parse(f, (char *)src.data);
    buf_free(&src);
    return m;
}

void test_opt_inline_cost_bonuses_os_and_single_site_multiplier(TestCtx *t)
{
    {
        InlineFix f;
        IrModule *m;
        OptConfig cfg;

        inline_fix_init(&f);
        m = cost_module(&f, false);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        opt_config_init(&cfg, OPT_O2);
        cfg.inline_threshold = 20;
        T_ASSERT(t, m && opt_inline(m, &cfg));
        T_ASSERT(t, m && func_op_count(&m->funcs[1], IR_CALL) == 0);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        arena_free_all(&f.arena);
    }
    {
        InlineFix f;
        IrModule *m;
        OptConfig cfg;
        FILE *report;
        char log[256];

        inline_fix_init(&f);
        m = cost_module(&f, false);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        report = tmpfile();
        opt_config_init(&cfg, OPT_OS);
        cfg.inline_threshold = 20;
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, m && !opt_inline(m, &cfg));
        T_ASSERT(t, m && func_op_count(&m->funcs[1], IR_CALL) == 1);
        if (report) {
            read_report(report, log, sizeof(log));
            T_ASSERT(t, strstr(log, "inl_cost") != NULL);
            fclose(report);
        }
        arena_free_all(&f.arena);
    }
    {
        InlineFix f;
        IrModule *m;
        OptConfig cfg;

        inline_fix_init(&f);
        m = cost_module(&f, true);
        T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
        opt_config_init(&cfg, OPT_O2);
        cfg.inline_threshold = 5;
        T_ASSERT(t, m && opt_inline(m, &cfg));
        T_ASSERT(t, m && func_op_count(&m->funcs[1], IR_CALL) == 0);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        arena_free_all(&f.arena);
    }
}

void test_opt_inline_threshold_config_table(TestCtx *t)
{
    static const struct {
        OptLevel level;
        u32 threshold;
    } rows[] = {
        {OPT_O0, 0},  {OPT_O1, 0},  {OPT_O2, 40},
        {OPT_O3, 80}, {OPT_OS, 20}, {OPT_OFAST, 80},
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        OptConfig cfg;

        opt_config_init(&cfg, rows[i].level);
        T_ASSERT_EQ_INT(t, cfg.inline_threshold, rows[i].threshold);
    }
}

void test_opt_inline_name_collision_and_roundtrip(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    IrModule *round;
    OptConfig cfg;
    Buf text;
    char *printed;

    inline_fix_init(&f);
    m = inline_parse(&f, "func i32 @id(i32 %x) {\n"
                         "entry():\n    ret i32 %x\n}\n"
                         "func i32 @caller(i32 %x) {\n"
                         "entry():\n"
                         "    %r = call i32 @id(i32 %x)\n"
                         "    br inl.0.b0(i32 %r)\n"
                         "inl.0.b0(i32 %v):\n"
                         "    ret i32 %v\n"
                         "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    T_ASSERT(t, m && opt_inline(m, &cfg));
    if (m) {
        printed = inline_print(m, &text);
        T_ASSERT(t, strstr(printed, "inl.1.b0") != NULL);
        round = ir_parse_module(&f.arena, f.dc, printed, "<inline-roundtrip>");
        T_ASSERT(t, round != NULL && ir_module_struct_eq(m, round));
        T_ASSERT(t, ir_verify(f.dc, m));
        buf_free(&text);
    }
    arena_free_all(&f.arena);
}
