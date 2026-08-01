#include "opt/opt.h"
#include "unit.h"

#include <stdio.h>
#include <string.h>

typedef struct VecFix {
    Arena arena;
    DiagCtx *dc;
} VecFix;

static void vec_silent(void *user, const Diag *diag, const DiagCtx *dc)
{
    (void)user;
    (void)diag;
    (void)dc;
}

static void vec_init(VecFix *fix)
{
    DiagSink sink = {vec_silent, NULL};

    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    diag_set_sink(fix->dc, sink);
}

static IrModule *vec_parse(VecFix *fix, const char *source)
{
    IrModule *m = ir_parse_module(&fix->arena, fix->dc, source, "<vectorize>");

    return m && ir_verify(fix->dc, m) ? m : NULL;
}

static u32 count_op(const IrFunc *f, IrOp op)
{
    u32 bi, count = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            count += in->op == op;
    }
    return count;
}

static IrModule *map_module(VecFix *fix, u32 trip, i64 step, bool same_base)
{
    char source[2048];

    snprintf(source, sizeof(source),
             "global @a size 8192 align 16 external\n"
             "global @b size 8192 align 16 external\n"
             "func void @map() {\n"
             "entry():\n"
             "    br loop(i64 0)\n"
             "loop(i64 %%i):\n"
             "    %%c = icmp ult i64 %%i, %u\n"
             "    condbr %%c, body(), exit()\n"
             "body():\n"
             "    %%off = imul i64 %%i, 4\n"
             "    %%ap = ptradd @a, %%off\n"
             "    %%bp = ptradd @%c, %%off\n"
             "    %%x = load i32, %%ap, align 4, etype i32\n"
             "    %%y = iadd i32 %%x, 3\n"
             "    store i32 %%y, %%bp, align 4, etype i32\n"
             "    %%next = iadd i64 %%i, %lld\n"
             "    br loop(i64 %%next)\n"
             "exit():\n"
             "    ret\n"
             "}\n",
             trip, same_base ? 'a' : 'b', (long long)step);
    return vec_parse(fix, source);
}

static IrModule *sum_module(VecFix *fix, u32 trip)
{
    char source[1600];

    snprintf(source, sizeof(source),
             "global @a size 8192 align 16 external\n"
             "func i32 @sum() {\n"
             "entry():\n"
             "    br loop(i64 0, i32 0)\n"
             "loop(i64 %%i, i32 %%sum):\n"
             "    %%c = icmp ult i64 %%i, %u\n"
             "    condbr %%c, body(), exit(i32 %%sum)\n"
             "body():\n"
             "    %%off = imul i64 %%i, 4\n"
             "    %%ap = ptradd @a, %%off\n"
             "    %%x = load i32, %%ap, align 4, etype i32\n"
             "    %%sum.next = iadd i32 %%sum, %%x, nsw\n"
             "    %%next = iadd i64 %%i, 1\n"
             "    br loop(i64 %%next, i32 %%sum.next)\n"
             "exit(i32 %%result):\n"
             "    ret i32 %%result\n"
             "}\n",
             trip);
    return vec_parse(fix, source);
}

static IrModule *integer_reduce_module(VecFix *fix, const char *op,
                                       const char *initial)
{
    char source[1600];

    snprintf(source, sizeof(source),
             "global @a size 128 align 16 external\n"
             "func i32 @reduce() {\n"
             "entry():\n"
             "    br loop(i64 0, i32 %s)\n"
             "loop(i64 %%i, i32 %%acc):\n"
             "    %%c = icmp ult i64 %%i, 8\n"
             "    condbr %%c, body(), exit(i32 %%acc)\n"
             "body():\n"
             "    %%off = imul i64 %%i, 4\n"
             "    %%ap = ptradd @a, %%off\n"
             "    %%x = load i32, %%ap, align 4, etype i32\n"
             "    %%acc.next = %s i32 %%acc, %%x\n"
             "    %%next = iadd i64 %%i, 1\n"
             "    br loop(i64 %%next, i32 %%acc.next)\n"
             "exit(i32 %%result):\n"
             "    ret i32 %%result\n"
             "}\n",
             initial, op);
    return vec_parse(fix, source);
}

static IrModule *fp_reduce_module(VecFix *fix, const char *op,
                                  const char *initial)
{
    char source[1600];

    snprintf(source, sizeof(source),
             "global @a size 128 align 16 external\n"
             "func f32 @reduce() {\n"
             "entry():\n"
             "    br loop(i64 0, f32 %s)\n"
             "loop(i64 %%i, f32 %%acc):\n"
             "    %%c = icmp ult i64 %%i, 8\n"
             "    condbr %%c, body(), exit(f32 %%acc)\n"
             "body():\n"
             "    %%off = imul i64 %%i, 4\n"
             "    %%ap = ptradd @a, %%off\n"
             "    %%x = load f32, %%ap, align 4, etype f32\n"
             "    %%acc.next = %s f32 %%acc, %%x\n"
             "    %%next = iadd i64 %%i, 1\n"
             "    br loop(i64 %%next, f32 %%acc.next)\n"
             "exit(f32 %%result):\n"
             "    ret f32 %%result\n"
             "}\n",
             initial, op);
    return vec_parse(fix, source);
}

static IrModule *typed_map_module(VecFix *fix, const char *type,
                                  const char *etype, const char *op,
                                  const char *constant, u32 stride, u32 trip)
{
    char source[1800];
    u32 align = strcmp(type, "i8") == 0                                  ? 1
                : strcmp(type, "i16") == 0                               ? 2
                : (strcmp(type, "i32") == 0 || strcmp(type, "f32") == 0) ? 4
                                                                         : 8;

    snprintf(source, sizeof(source),
             "global @a size 8192 align 16 external\n"
             "global @b size 8192 align 16 external\n"
             "func void @typed_map() {\n"
             "entry():\n"
             "    br loop(i64 0)\n"
             "loop(i64 %%i):\n"
             "    %%c = icmp ult i64 %%i, %u\n"
             "    condbr %%c, body(), exit()\n"
             "body():\n"
             "    %%off = imul i64 %%i, %u\n"
             "    %%ap = ptradd @a, %%off\n"
             "    %%bp = ptradd @b, %%off\n"
             "    %%x = load %s, %%ap, align %u, etype %s\n"
             "    %%y = %s %s %%x, %s\n"
             "    store %s %%y, %%bp, align %u, etype %s\n"
             "    %%next = iadd i64 %%i, 1\n"
             "    br loop(i64 %%next)\n"
             "exit():\n"
             "    ret\n"
             "}\n",
             trip, stride, type, align, etype, op, type, constant, type, align,
             etype);
    return vec_parse(fix, source);
}

static bool report_has(FILE *report, const char *needle)
{
    char text[4096];
    size_t n;

    fflush(report);
    rewind(report);
    n = fread(text, 1, sizeof(text) - 1, report);
    text[n] = '\0';
    return strstr(text, needle) != NULL;
}

void test_vectorize_s36_exact_map_and_idempotence(TestCtx *t)
{
    static const u32 trips[] = {8, 1000};
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(trips); i++) {
        VecFix fix;
        IrModule *m;
        OptConfig cfg;

        vec_init(&fix);
        m = map_module(&fix, trips[i], 1, false);
        T_ASSERT(t, m != NULL);
        opt_config_init(&cfg, OPT_O3);
        cfg.verify_after_each = true;
        T_ASSERT(t, m && opt_vectorize(m, &cfg));
        if (m) {
            T_ASSERT(t, ir_verify(fix.dc, m));
            T_ASSERT(t, opt_func_has_vector_ir(&m->funcs[0]));
            T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_LOAD), 1);
            T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_STORE), 1);
            T_ASSERT(t, !opt_vectorize(m, &cfg));
            T_ASSERT(t, ir_verify(fix.dc, m));
        }
        arena_free_all(&fix.arena);
    }
}

void test_vectorize_s36_constant_prefix_and_integer_reduction(TestCtx *t)
{
    u32 trip;

    for (trip = 9; trip <= 1001; trip += 992) {
        VecFix fix;
        IrModule *m;
        OptConfig cfg;

        vec_init(&fix);
        m = sum_module(&fix, trip);
        T_ASSERT(t, m != NULL);
        opt_config_init(&cfg, OPT_O3);
        cfg.verify_after_each = true;
        T_ASSERT(t, m && opt_vectorize(m, &cfg));
        if (m) {
            T_ASSERT(t, ir_verify(fix.dc, m));
            T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_VREDUCE_ADD), 1);
            /* One scalar prefix load remains in addition to the vector load. */
            T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_LOAD), 2);
            T_ASSERT(t, !opt_vectorize(m, &cfg));
        }
        arena_free_all(&fix.arena);
    }
}

void test_vectorize_s36_level_cost_trip_and_stride_bails(TestCtx *t)
{
    u32 row;

    for (row = 0; row < 4; row++) {
        VecFix fix;
        IrModule *m;
        OptConfig cfg;
        FILE *report = tmpfile();

        vec_init(&fix);
        m = map_module(&fix, row == 1 ? 7 : 8, row == 2 ? 2 : 1, false);
        T_ASSERT(t, m != NULL && report != NULL);
        opt_config_init(&cfg, row == 0 ? OPT_O2 : OPT_O3);
        cfg.bail_log = true;
        cfg.report = report;
        if (row == 3)
            cfg.disable_vectorize = true;
        T_ASSERT(t, m && !opt_vectorize(m, &cfg));
        if (row == 1)
            T_ASSERT(t, report_has(report, "vec_cost"));
        if (row == 2)
            T_ASSERT(t, report_has(report, "vec_trip_unknown"));
        if (row == 3)
            T_ASSERT(t, !opt_vectorize(m, &cfg));
        if (m)
            T_ASSERT(t, ir_verify(fix.dc, m));
        if (report)
            fclose(report);
        arena_free_all(&fix.arena);
    }
}

void test_vectorize_s36_distance_and_alias_bails(TestCtx *t)
{
    VecFix fix;
    IrModule *m;
    OptConfig cfg;

    vec_init(&fix);
    m = map_module(&fix, 8, 1, true);
    T_ASSERT(t, m != NULL);
    opt_config_init(&cfg, OPT_O3);
    T_ASSERT(t, m && opt_vectorize(m, &cfg));
    if (m)
        T_ASSERT(t, ir_verify(fix.dc, m));
    arena_free_all(&fix.arena);

    vec_init(&fix);
    m = vec_parse(&fix, "func void @may_alias(ptr %a, ptr %b) {\n"
                        "entry():\n"
                        "    br loop(i64 0)\n"
                        "loop(i64 %i):\n"
                        "    %c = icmp ult i64 %i, 8\n"
                        "    condbr %c, body(), exit()\n"
                        "body():\n"
                        "    %off = imul i64 %i, 4\n"
                        "    %ap = ptradd %a, %off\n"
                        "    %bp = ptradd %b, %off\n"
                        "    %x = load i32, %ap, align 4, etype i32\n"
                        "    store i32 %x, %bp, align 4, etype i32\n"
                        "    %next = iadd i64 %i, 1\n"
                        "    br loop(i64 %next)\n"
                        "exit():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    {
        FILE *report = tmpfile();

        opt_config_init(&cfg, OPT_O3);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, m && !opt_vectorize(m, &cfg));
        T_ASSERT(t, report && report_has(report, "vec_alias_unproven"));
        if (report)
            fclose(report);
    }
    if (m)
        T_ASSERT(t, ir_verify(fix.dc, m));
    arena_free_all(&fix.arena);
}

void test_vectorize_s36_descriptor_and_pipeline_controls(TestCtx *t)
{
    T_ASSERT_EQ_STR(t, OPT_PASS_VECTORIZE.name, "vectorize");
    T_ASSERT_EQ_INT(t, OPT_PASS_VECTORIZE.pinned_policy, PASS_PINNED_EXACT);
}

void test_vectorize_s36_all_integer_reductions(TestCtx *t)
{
    static const struct {
        const char *op;
        const char *initial;
        IrOp reduce;
    } rows[] = {{"iadd", "0", IR_VREDUCE_ADD},
                {"iadd", "17", IR_VREDUCE_ADD},
                {"and", "-1", IR_VREDUCE_AND},
                {"or", "0", IR_VREDUCE_OR},
                {"xor", "0", IR_VREDUCE_XOR}};
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        VecFix fix;
        IrModule *m;
        OptConfig cfg;

        vec_init(&fix);
        m = integer_reduce_module(&fix, rows[i].op, rows[i].initial);
        T_ASSERT(t, m != NULL);
        opt_config_init(&cfg, OPT_O3);
        T_ASSERT(t, m && opt_vectorize(m, &cfg));
        if (m) {
            T_ASSERT(t, ir_verify(fix.dc, m));
            T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], rows[i].reduce), 1);
        }
        arena_free_all(&fix.arena);
    }
}

void test_vectorize_s36_six_type_sse2_operation_matrix(TestCtx *t)
{
    static const struct {
        const char *type, *etype, *op, *constant;
        u32 stride, trip;
        IrType vector;
    } rows[] = {
        {"i8", "i8", "iadd", "1", 1, 32, IRT_V16I8},
        {"i8", "i8", "isub", "1", 1, 32, IRT_V16I8},
        {"i8", "i8", "and", "127", 1, 32, IRT_V16I8},
        {"i8", "i8", "or", "1", 1, 32, IRT_V16I8},
        {"i8", "i8", "xor", "1", 1, 32, IRT_V16I8},
        {"i16", "i16", "iadd", "1", 2, 16, IRT_V8I16},
        {"i16", "i16", "isub", "1", 2, 16, IRT_V8I16},
        {"i16", "i16", "imul", "3", 2, 16, IRT_V8I16},
        {"i16", "i16", "and", "255", 2, 16, IRT_V8I16},
        {"i16", "i16", "or", "1", 2, 16, IRT_V8I16},
        {"i16", "i16", "xor", "1", 2, 16, IRT_V8I16},
        {"i32", "i32", "iadd", "1", 4, 8, IRT_V4I32},
        {"i32", "i32", "isub", "1", 4, 8, IRT_V4I32},
        {"i32", "i32", "and", "255", 4, 8, IRT_V4I32},
        {"i32", "i32", "or", "1", 4, 8, IRT_V4I32},
        {"i32", "i32", "xor", "1", 4, 8, IRT_V4I32},
        {"i64", "i64", "iadd", "1", 8, 4, IRT_V2I64},
        {"i64", "i64", "isub", "1", 8, 4, IRT_V2I64},
        {"i64", "i64", "and", "255", 8, 4, IRT_V2I64},
        {"i64", "i64", "or", "1", 8, 4, IRT_V2I64},
        {"i64", "i64", "xor", "1", 8, 4, IRT_V2I64},
        {"f32", "f32", "fadd", "0x3F800000", 4, 8, IRT_V4F32},
        {"f32", "f32", "fsub", "0x3F800000", 4, 8, IRT_V4F32},
        {"f32", "f32", "fmul", "0x3F800000", 4, 8, IRT_V4F32},
        {"f32", "f32", "fdiv", "0x3F800000", 4, 8, IRT_V4F32},
        {"f64", "f64", "fadd", "0x3FF0000000000000", 8, 4, IRT_V2F64},
        {"f64", "f64", "fsub", "0x3FF0000000000000", 8, 4, IRT_V2F64},
        {"f64", "f64", "fmul", "0x3FF0000000000000", 8, 4, IRT_V2F64},
        {"f64", "f64", "fdiv", "0x3FF0000000000000", 8, 4, IRT_V2F64},
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        VecFix fix;
        IrModule *m;
        OptConfig cfg;
        u32 bi;
        bool saw = false;

        vec_init(&fix);
        m = typed_map_module(&fix, rows[i].type, rows[i].etype, rows[i].op,
                             rows[i].constant, rows[i].stride, rows[i].trip);
        T_ASSERT(t, m != NULL);
        opt_config_init(&cfg, OPT_O3);
        T_ASSERT(t, m && opt_vectorize(m, &cfg));
        if (m) {
            for (bi = 0; bi < m->funcs[0].nblocks; bi++) {
                const IrInst *in;
                for (in = m->funcs[0].blocks[bi].first; in; in = in->next)
                    saw |= (IrType)in->type == rows[i].vector;
            }
            T_ASSERT(t, saw);
            T_ASSERT(t, ir_verify(fix.dc, m));
        }
        arena_free_all(&fix.arena);
    }
}

void test_vectorize_s36_fp_reductions_require_reassociation(TestCtx *t)
{
    static const struct {
        const char *op;
        const char *initial;
        IrOp reduce;
    } rows[] = {{"fadd", "0x00000000", IR_VREDUCE_ADD},
                {"fmul", "0x3F800000", IR_VREDUCE_MUL}};
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        VecFix strict_fix, fast_fix;
        IrModule *strict_m, *fast_m;
        OptConfig cfg;
        FILE *report;

        vec_init(&strict_fix);
        strict_m = fp_reduce_module(&strict_fix, rows[i].op, rows[i].initial);
        report = tmpfile();
        T_ASSERT(t, strict_m != NULL && report != NULL);
        opt_config_init(&cfg, OPT_O3);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, strict_m && !opt_vectorize(strict_m, &cfg));
        T_ASSERT(t,
                 report && report_has(report, "vec_fp_reduction_needs_ofast"));
        if (report)
            fclose(report);
        arena_free_all(&strict_fix.arena);

        vec_init(&fast_fix);
        fast_m = fp_reduce_module(&fast_fix, rows[i].op, rows[i].initial);
        T_ASSERT(t, fast_m != NULL);
        opt_config_init(&cfg, OPT_OFAST);
        T_ASSERT(t, fast_m && opt_vectorize(fast_m, &cfg));
        if (fast_m) {
            T_ASSERT(t, ir_verify(fast_fix.dc, fast_m));
            T_ASSERT_EQ_INT(t, count_op(&fast_m->funcs[0], rows[i].reduce), 1);
        }
        arena_free_all(&fast_fix.arena);
    }
}

void test_vectorize_s36_later_loop_passes_skip_vector_functions(TestCtx *t)
{
    VecFix fix;
    IrModule *m;
    OptConfig cfg;

    vec_init(&fix);
    m = map_module(&fix, 8, 1, false);
    T_ASSERT(t, m != NULL);
    opt_config_init(&cfg, OPT_O3);
    T_ASSERT(t, m && opt_vectorize(m, &cfg));
    if (m) {
        T_ASSERT(t, !opt_licm(m, &cfg));
        T_ASSERT(t, !opt_strength(m, &cfg));
        T_ASSERT(t, !opt_bce(m, &cfg));
        T_ASSERT(t, !opt_unswitch(m, &cfg));
        T_ASSERT(t, !opt_unroll(m, &cfg));
        T_ASSERT(t, !opt_fusion(m, &cfg));
        T_ASSERT(t, ir_verify(fix.dc, m));
    }
    arena_free_all(&fix.arena);
}

void test_vectorize_s36_remaining_named_legality_bails(TestCtx *t)
{
    static const struct {
        const char *reason;
        const char *source;
    } rows[] = {
        {"vec_trip_unknown", "global @a size 128 align 16 external\n"
                             "func void @runtime(i64 %n) {\n"
                             "entry(): br loop(i64 0)\n"
                             "loop(i64 %i):\n"
                             "  %c = icmp ult i64 %i, %n\n"
                             "  condbr %c, body(), exit()\n"
                             "body():\n"
                             "  %off = imul i64 %i, 4\n"
                             "  %p = ptradd @a, %off\n"
                             "  store i32 1, %p, align 4, etype i32\n"
                             "  %next = iadd i64 %i, 1\n"
                             "  br loop(i64 %next)\n"
                             "exit(): ret\n"
                             "}\n"},
        {"vec_loop_carried", "global @a size 256 align 16 external\n"
                             "func void @carried() {\n"
                             "entry(): br loop(i64 0)\n"
                             "loop(i64 %i):\n"
                             "  %c = icmp ult i64 %i, 8\n"
                             "  condbr %c, body(), exit()\n"
                             "body():\n"
                             "  %off = imul i64 %i, 4\n"
                             "  %next.off = iadd i64 %off, 4\n"
                             "  %p = ptradd @a, %off\n"
                             "  %q = ptradd @a, %next.off\n"
                             "  %x = load i32, %p, align 4, etype i32\n"
                             "  store i32 %x, %q, align 4, etype i32\n"
                             "  %next = iadd i64 %i, 1\n"
                             "  br loop(i64 %next)\n"
                             "exit(): ret\n"
                             "}\n"},
        {"vec_body_op", "global @a size 128 align 16 external\n"
                        "func void @volatile_body() {\n"
                        "entry(): br loop(i64 0)\n"
                        "loop(i64 %i):\n"
                        "  %c = icmp ult i64 %i, 8\n"
                        "  condbr %c, body(), exit()\n"
                        "body():\n"
                        "  %off = imul i64 %i, 4\n"
                        "  %p = ptradd @a, %off\n"
                        "  store i32 1, %p, align 4, volatile, etype i32\n"
                        "  %next = iadd i64 %i, 1\n"
                        "  br loop(i64 %next)\n"
                        "exit(): ret\n"
                        "}\n"},
        {"vec_control_flow", "global @a size 128 align 16 external\n"
                             "func void @control(i32 %pick) {\n"
                             "entry(): br loop(i64 0)\n"
                             "loop(i64 %i):\n"
                             "  %c = icmp ult i64 %i, 8\n"
                             "  condbr %c, body(), exit()\n"
                             "body(): condbr %pick, left(), right()\n"
                             "left():\n"
                             "  %ln = iadd i64 %i, 1\n"
                             "  br loop(i64 %ln)\n"
                             "right():\n"
                             "  %rn = iadd i64 %i, 1\n"
                             "  br loop(i64 %rn)\n"
                             "exit(): ret\n"
                             "}\n"},
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        VecFix fix;
        IrModule *m;
        OptConfig cfg;
        FILE *report = tmpfile();

        vec_init(&fix);
        m = vec_parse(&fix, rows[i].source);
        T_ASSERT(t, m != NULL && report != NULL);
        opt_config_init(&cfg, OPT_O3);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, m && !opt_vectorize(m, &cfg));
        T_ASSERT(t, report && report_has(report, rows[i].reason));
        if (m)
            T_ASSERT(t, ir_verify(fix.dc, m));
        if (report)
            fclose(report);
        arena_free_all(&fix.arena);
    }

    {
        VecFix fix;
        IrModule *m;
        OptConfig cfg;
        FILE *report = tmpfile();

        vec_init(&fix);
        m = typed_map_module(&fix, "i32", "i32", "iadd", "1", 8, 8);
        T_ASSERT(t, m != NULL && report != NULL);
        opt_config_init(&cfg, OPT_O3);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, m && !opt_vectorize(m, &cfg));
        T_ASSERT(t, report && report_has(report, "vec_stride"));
        if (report)
            fclose(report);
        arena_free_all(&fix.arena);
    }
}

void test_vectorize_s36_source_lcssa_and_lane_state_guards(TestCtx *t)
{
    static const char source_reduction[] =
        "global @a size 128 align 16 external\n"
        "func i32 @source_sum() {\n"
        "entry(): br loop(i64 0, i32 0)\n"
        "loop(i64 %i, i32 %sum):\n"
        "  %c = icmp ult i64 %i, 9\n"
        "  condbr %c, body(), exit()\n"
        "body():\n"
        "  %off = imul i64 %i, 4\n"
        "  %p = ptradd @a, %off\n"
        "  %x = load i32, %p, align 4, etype i32\n"
        "  %sum.next = iadd i32 %sum, %x\n"
        "  %next = iadd i64 %i, 1\n"
        "  br loop(i64 %next, i32 %sum.next)\n"
        "exit(): ret i32 %sum\n"
        "}\n";
    static const char lane_iv[] = "global @a size 128 align 16 external\n"
                                  "func void @lane_iv() {\n"
                                  "entry(): br loop(i64 0)\n"
                                  "loop(i64 %i):\n"
                                  "  %c = icmp ult i64 %i, 8\n"
                                  "  condbr %c, body(), exit()\n"
                                  "body():\n"
                                  "  %off = imul i64 %i, 8\n"
                                  "  %p = ptradd @a, %off\n"
                                  "  store i64 %i, %p, align 8, etype i64\n"
                                  "  %next = iadd i64 %i, 1\n"
                                  "  br loop(i64 %next)\n"
                                  "exit(): ret\n"
                                  "}\n";
    static const char scan_store[] =
        "global @a size 128 align 16 external\n"
        "global @out size 128 align 16 external\n"
        "func i32 @scan_store() {\n"
        "entry(): br loop(i64 0, i32 0)\n"
        "loop(i64 %i, i32 %sum):\n"
        "  %c = icmp ult i64 %i, 8\n"
        "  condbr %c, body(), exit()\n"
        "body():\n"
        "  %off = imul i64 %i, 4\n"
        "  %p = ptradd @a, %off\n"
        "  %q = ptradd @out, %off\n"
        "  %x = load i32, %p, align 4, etype i32\n"
        "  %sum.next = iadd i32 %sum, %x\n"
        "  store i32 %sum.next, %q, align 4, etype i32\n"
        "  %next = iadd i64 %i, 1\n"
        "  br loop(i64 %next, i32 %sum.next)\n"
        "exit(): ret i32 %sum\n"
        "}\n";
    VecFix fix;
    IrModule *m;
    OptConfig cfg;
    u32 row;

    vec_init(&fix);
    m = vec_parse(&fix, source_reduction);
    T_ASSERT(t, m != NULL);
    opt_config_init(&cfg, OPT_O3);
    T_ASSERT(t, m && opt_vectorize(m, &cfg));
    if (m) {
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_VREDUCE_ADD), 1);
        T_ASSERT(t, ir_verify(fix.dc, m));
    }
    arena_free_all(&fix.arena);

    for (row = 0; row < 2; row++) {
        FILE *report = tmpfile();

        vec_init(&fix);
        m = vec_parse(&fix, row == 0 ? lane_iv : scan_store);
        T_ASSERT(t, m != NULL && report != NULL);
        opt_config_init(&cfg, OPT_O3);
        cfg.bail_log = true;
        cfg.report = report;
        if (m)
            (void)opt_vectorize(m, &cfg);
        T_ASSERT_EQ_INT(t, m ? count_op(&m->funcs[0], IR_VSPLAT) : 0, 0);
        T_ASSERT_EQ_INT(t, m ? count_op(&m->funcs[0], IR_VREDUCE_ADD) : 0, 0);
        T_ASSERT(t, report && report_has(report, "vec_body_op"));
        if (m)
            T_ASSERT(t, ir_verify(fix.dc, m));
        if (report)
            fclose(report);
        arena_free_all(&fix.arena);
    }
}
