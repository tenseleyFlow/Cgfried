#include <stdio.h>
#include <string.h>

#include "opt/dep.h"
#include "opt/opt.h"
#include "unit.h"
#include "util/arena.h"

typedef struct DepFix {
    Arena arena;
    DiagCtx *dc;
} DepFix;

static void dep_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void dep_fix_init(DepFix *fix)
{
    DiagSink sink = {dep_silent_sink, NULL};

    arena_init(&fix->arena);
    fix->dc = diag_ctx_new(&fix->arena);
    diag_set_sink(fix->dc, sink);
}

static IrModule *dep_parse(DepFix *fix, const char *text)
{
    IrModule *m = ir_parse_module(&fix->arena, fix->dc, text, "<dep-test>");

    return m && ir_verify(fix->dc, m) ? m : NULL;
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

static u32 count_op(const IrFunc *f, IrOp op)
{
    u32 bi, n = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;
        for (in = f->blocks[bi].first; in; in = in->next)
            n += in->op == op;
    }
    return n;
}

void test_dep_exact_distance_table_and_independence_are_distinct(TestCtx *t)
{
    typedef struct Row {
        i64 stride, a, b;
        u64 size;
        DepKind kind;
        i64 distance;
    } Row;
    static const Row rows[] = {
        {1, 0, 0, 1, DEP_DISTANCE, 0},
        {1, 3, 0, 1, DEP_DISTANCE, 3},
        {1, 0, 3, 1, DEP_DISTANCE, -3},
        {2, 6, 0, 1, DEP_DISTANCE, 3},
        {2, 0, 6, 1, DEP_DISTANCE, -3},
        {4, 12, 0, 4, DEP_DISTANCE, 3},
        {4, 0, 12, 4, DEP_DISTANCE, -3},
        {4, 4, 4, 4, DEP_DISTANCE, 0},
        {-4, 12, 0, 4, DEP_DISTANCE, -3},
        {-4, 0, 12, 4, DEP_DISTANCE, 3},
        {8, 24, 0, 8, DEP_DISTANCE, 3},
        {8, 0, 24, 8, DEP_DISTANCE, -3},
        {4, 1, 0, 1, DEP_INDEPENDENT, 0},
        {4, 2, 0, 1, DEP_INDEPENDENT, 0},
        {4, 3, 0, 1, DEP_INDEPENDENT, 0},
        {8, 2, 0, 2, DEP_INDEPENDENT, 0},
        {8, 4, 0, 4, DEP_INDEPENDENT, 0},
        {16, 8, 0, 8, DEP_INDEPENDENT, 0},
        {4, 1, 0, 2, DEP_UNKNOWN, 0},
        {4, 2, 0, 3, DEP_UNKNOWN, 0},
        {4, 0, 0, 8, DEP_UNKNOWN, 0},
        {0, 0, 0, 1, DEP_UNKNOWN, 0},
        {4, INT64_MAX, -1, 1, DEP_UNKNOWN, 0},
        {4, 0, 0, 2, DEP_DISTANCE, 0},
    };
    DepFix fix;
    IrModule *m;
    AliasConfig cfg;
    AliasCtx *alias;
    IrOperand base;
    u32 i;

    dep_fix_init(&fix);
    m = dep_parse(&fix, "global @g size 64 align 8 external\n"
                        "func void @f() {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    cfg = (AliasConfig){&m->funcs[0], false};
    alias = alias_build(m, &cfg);
    base = ir_op_symbol(IRT_PTR, ir_sym(m, "g"), 0);
    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        DepAccess a = {base, rows[i].stride, rows[i].a, rows[i].size,
                       ETYPE_I32};
        DepAccess b = {base, rows[i].stride, rows[i].b, rows[i].size,
                       ETYPE_I32};
        DepResult got = dep_query(alias, a, b);

        T_ASSERT_EQ_INT(t, got.kind, rows[i].kind);
        if (got.kind == DEP_DISTANCE)
            T_ASSERT_EQ_INT(t, got.distance, rows[i].distance);
    }
    alias_free(alias);
    arena_free_all(&fix.arena);
}

void test_dep_distinct_bases_and_may_alias_bases(TestCtx *t)
{
    DepFix fix;
    IrModule *m;
    AliasConfig cfg;
    AliasCtx *alias;
    DepAccess a, b;
    DepResult got;

    dep_fix_init(&fix);
    m = dep_parse(&fix, "global @g size 64 align 8 external\n"
                        "global @h size 64 align 8 external\n"
                        "func void @f(ptr %p, ptr %q) {\n"
                        "entry():\n"
                        "    %base = alloca 64, align 8\n"
                        "    %shifted = ptradd %base, 4\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    cfg = (AliasConfig){&m->funcs[0], false};
    alias = alias_build(m, &cfg);
    a = (DepAccess){ir_op_symbol(IRT_PTR, ir_sym(m, "g"), 0), 4, 0, 4,
                    ETYPE_I32};
    b = (DepAccess){ir_op_symbol(IRT_PTR, ir_sym(m, "h"), 0), 4, 0, 4,
                    ETYPE_I32};
    got = dep_query(alias, a, b);
    T_ASSERT_EQ_INT(t, got.kind, DEP_INDEPENDENT);
    a.base = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[0]);
    b.base = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[1]);
    got = dep_query(alias, a, b);
    T_ASSERT_EQ_INT(t, got.kind, DEP_UNKNOWN);
    T_ASSERT_EQ_STR(t, got.reason, "dep_bases_may_alias");
    a.base =
        ir_op_value(&m->funcs[0], find_op(&m->funcs[0], IR_ALLOCA, 0)->result);
    b.base =
        ir_op_value(&m->funcs[0], find_op(&m->funcs[0], IR_PTRADD, 0)->result);
    got = dep_query(alias, a, b);
    T_ASSERT_EQ_INT(t, got.kind, DEP_UNKNOWN);
    T_ASSERT_EQ_STR(t, got.reason, "dep_bases_may_alias");
    alias_free(alias);
    arena_free_all(&fix.arena);
}

void test_dep_ptr_recognizer_accepts_k_i_plus_c_and_rejects_nonaffine(
    TestCtx *t)
{
    DepFix fix;
    IrModule *m;
    IrFunc *f;
    IrInst *good, *shifted, *bad;
    DepAccess access;
    const char *reason = NULL;

    dep_fix_init(&fix);
    m = dep_parse(&fix, "func void @f(ptr %p, i64 %i, i64 %j) {\n"
                        "entry():\n"
                        "    %scale = imul i64 %i, 4\n"
                        "    %off = iadd i64 %scale, 12\n"
                        "    %good = ptradd %p, %off\n"
                        "    %shift = shl i64 %i, 2\n"
                        "    %shifted = ptradd %p, %shift\n"
                        "    %prod = imul i64 %i, %j\n"
                        "    %bad = ptradd %p, %prod\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    f = &m->funcs[0];
    good = find_op(f, IR_PTRADD, 0);
    shifted = find_op(f, IR_PTRADD, 1);
    bad = find_op(f, IR_PTRADD, 2);
    T_ASSERT(t, dep_access_from_ptr(f, ir_op_value(f, good->result),
                                    f->param_vals[1], 4, ETYPE_I32, &access,
                                    &reason));
    T_ASSERT_EQ_INT(t, access.stride, 4);
    T_ASSERT_EQ_INT(t, access.offset, 12);
    T_ASSERT(t, dep_access_from_ptr(f, ir_op_value(f, shifted->result),
                                    f->param_vals[1], 4, ETYPE_I32, &access,
                                    &reason));
    T_ASSERT_EQ_INT(t, access.stride, 4);
    T_ASSERT_EQ_INT(t, access.offset, 0);
    T_ASSERT(t, !dep_access_from_ptr(f, ir_op_value(f, bad->result),
                                     f->param_vals[1], 4, ETYPE_I32, &access,
                                     &reason));
    T_ASSERT_EQ_STR(t, reason, "dep_nonaffine");
    arena_free_all(&fix.arena);
}

static const char *fusion_ir(bool negative, bool mismatch)
{
    if (negative)
        return "func void @f() {\n"
               "entry():\n"
               "    %a = alloca 64, align 8\n"
               "    br a.h(i64 0)\n"
               "a.h(i64 %i):\n"
               "    %ac = icmp ult i64 %i, 4\n"
               "    condbr %ac, a.body(), middle()\n"
               "a.body():\n"
               "    %ao = imul i64 %i, 4\n"
               "    %ap = ptradd %a, %ao\n"
               "    store i32 1, %ap, align 4, etype i32\n"
               "    %an = iadd i64 %i, 1\n"
               "    br a.h(i64 %an)\n"
               "middle():\n"
               "    br b.h(i64 0)\n"
               "b.h(i64 %j):\n"
               "    %bc = icmp ult i64 %j, 4\n"
               "    condbr %bc, b.body(), exit()\n"
               "b.body():\n"
               "    %bo = imul i64 %j, 4\n"
               "    %boff = iadd i64 %bo, 4\n"
               "    %bp = ptradd %a, %boff\n"
               "    store i32 2, %bp, align 4, etype i32\n"
               "    %bn = iadd i64 %j, 1\n"
               "    br b.h(i64 %bn)\n"
               "exit():\n"
               "    ret\n"
               "}\n";
    if (mismatch)
        return "func void @f() {\n"
               "entry():\n"
               "    br a.h(i64 0)\n"
               "a.h(i64 %i):\n"
               "    %ac = icmp ult i64 %i, 4\n"
               "    condbr %ac, a.body(), middle()\n"
               "a.body():\n"
               "    %an = iadd i64 %i, 1\n"
               "    br a.h(i64 %an)\n"
               "middle():\n"
               "    br b.h(i64 0)\n"
               "b.h(i64 %j):\n"
               "    %bc = icmp ult i64 %j, 5\n"
               "    condbr %bc, b.body(), exit()\n"
               "b.body():\n"
               "    %bn = iadd i64 %j, 1\n"
               "    br b.h(i64 %bn)\n"
               "exit():\n"
               "    ret\n"
               "}\n";
    return "func void @f() {\n"
           "entry():\n"
           "    %a = alloca 64, align 8\n"
           "    %b = alloca 64, align 8\n"
           "    br a.h(i64 0)\n"
           "a.h(i64 %i):\n"
           "    %ac = icmp ult i64 %i, 4\n"
           "    condbr %ac, a.body(), middle()\n"
           "a.body():\n"
           "    %ao = imul i64 %i, 4\n"
           "    %ap = ptradd %a, %ao\n"
           "    store i32 1, %ap, align 4, etype i32\n"
           "    %an = iadd i64 %i, 1\n"
           "    br a.h(i64 %an)\n"
           "middle():\n"
           "    br b.h(i64 0)\n"
           "b.h(i64 %j):\n"
           "    %bc = icmp ult i64 %j, 4\n"
           "    condbr %bc, b.body(), exit()\n"
           "b.body():\n"
           "    %proof = icmp ult i64 %j, 4, bounds\n"
           "    %bo = imul i64 %j, 4\n"
           "    %bp = ptradd %b, %bo\n"
           "    store i32 2, %bp, align 4, etype i32\n"
           "    %bn = iadd i64 %j, 1\n"
           "    br b.h(i64 %bn)\n"
           "exit():\n"
           "    ret\n"
           "}\n";
}

void test_fusion_adjacent_equal_trip_loops(TestCtx *t)
{
    DepFix fix;
    IrModule *m;
    OptConfig cfg;

    dep_fix_init(&fix);
    T_ASSERT_EQ_STR(t, OPT_PASS_FUSION.name, "fusion");
    T_ASSERT_EQ_INT(t, OPT_PASS_FUSION.pinned_policy, PASS_PINNED_EXACT);
    m = dep_parse(&fix, fusion_ir(false, false));
    T_ASSERT(t, m != NULL);
    opt_config_init(&cfg, OPT_O3);
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_fusion(m, &cfg));
    if (m) {
        IrInst *proof;

        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, 4);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_STORE), 2);
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_CONDBR), 1);
        proof = find_op(&m->funcs[0], IR_ICMP, 1);
        T_ASSERT(t, proof != NULL && (proof->flags & IRF_BOUNDS_CHECK));
    }
    arena_free_all(&fix.arena);
}

void test_fusion_negative_distance_and_trip_mismatch_bail(TestCtx *t)
{
    u32 i;

    for (i = 0; i < 2; i++) {
        DepFix fix;
        IrModule *m;
        OptConfig cfg;
        FILE *report;
        char text[2048];
        size_t n;

        dep_fix_init(&fix);
        m = dep_parse(&fix, fusion_ir(i == 0, i == 1));
        T_ASSERT(t, m != NULL);
        report = tmpfile();
        T_ASSERT(t, report != NULL);
        opt_config_init(&cfg, OPT_O3);
        cfg.bail_log = true;
        cfg.report = report;
        T_ASSERT(t, m && !opt_fusion(m, &cfg));
        fflush(report);
        rewind(report);
        n = fread(text, 1, sizeof(text) - 1, report);
        text[n] = '\0';
        T_ASSERT(t, strstr(text, i == 0 ? "fuse_negative_dep"
                                        : "fuse_trip_mismatch") != NULL);
        if (m)
            T_ASSERT(t, ir_verify(fix.dc, m));
        fclose(report);
        arena_free_all(&fix.arena);
    }
}

void test_fusion_pinned_body_bails_intervening_without_mutation(TestCtx *t)
{
    DepFix fix;
    IrModule *m;
    OptConfig cfg;
    IrInst *store;
    FILE *report;
    char text[1024];
    size_t n;
    u32 before_blocks;

    dep_fix_init(&fix);
    m = dep_parse(&fix, fusion_ir(false, false));
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&fix.arena);
        return;
    }
    store = find_op(&m->funcs[0], IR_STORE, 0);
    store->flags |= IRF_VOLATILE;
    T_ASSERT(t, ir_verify(fix.dc, m));
    before_blocks = m->funcs[0].nblocks;
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O3);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, !opt_fusion(m, &cfg));
    fflush(report);
    rewind(report);
    n = fread(text, 1, sizeof(text) - 1, report);
    text[n] = '\0';
    T_ASSERT(t, strstr(text, "fuse_intervening") != NULL);
    T_ASSERT_EQ_INT(t, m->funcs[0].nblocks, before_blocks);
    T_ASSERT(t, ir_verify(fix.dc, m));
    fclose(report);
    arena_free_all(&fix.arena);
}

void test_fusion_rejects_shifted_same_object_bases(TestCtx *t)
{
    DepFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char text[1024];
    size_t n;

    dep_fix_init(&fix);
    m = dep_parse(&fix, "func void @f() {\n"
                        "entry():\n"
                        "    %base = alloca 128, align 8\n"
                        "    %shifted = ptradd %base, 4\n"
                        "    br a.h(i64 0)\n"
                        "a.h(i64 %i):\n"
                        "    %ac = icmp ult i64 %i, 20\n"
                        "    condbr %ac, a.body(), middle()\n"
                        "a.body():\n"
                        "    %ao = imul i64 %i, 4\n"
                        "    %ap = ptradd %base, %ao\n"
                        "    store i32 1, %ap, align 4, etype i32\n"
                        "    %an = iadd i64 %i, 1\n"
                        "    br a.h(i64 %an)\n"
                        "middle():\n"
                        "    br b.h(i64 0)\n"
                        "b.h(i64 %j):\n"
                        "    %bc = icmp ult i64 %j, 20\n"
                        "    condbr %bc, b.body(), exit()\n"
                        "b.body():\n"
                        "    %bo = imul i64 %j, 4\n"
                        "    %bp = ptradd %shifted, %bo\n"
                        "    store i32 2, %bp, align 4, etype i32\n"
                        "    %bn = iadd i64 %j, 1\n"
                        "    br b.h(i64 %bn)\n"
                        "exit():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O3);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_fusion(m, &cfg));
    fflush(report);
    rewind(report);
    n = fread(text, 1, sizeof(text) - 1, report);
    text[n] = '\0';
    T_ASSERT(t, strstr(text, "dep_bases_may_alias") != NULL);
    if (m) {
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_CONDBR), 2);
    }
    fclose(report);
    arena_free_all(&fix.arena);
}

void test_fusion_rejects_runtime_trip_without_termination_proof(TestCtx *t)
{
    DepFix fix;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    char text[1024];
    size_t n;

    dep_fix_init(&fix);
    m = dep_parse(&fix,
                  "func void @f(ptr restrict %a, ptr restrict %b, i64 %n) {\n"
                  "entry():\n"
                  "    br a.h(i64 0)\n"
                  "a.h(i64 %i):\n"
                  "    %ac = icmp ult i64 %i, %n\n"
                  "    condbr %ac, a.body(), middle()\n"
                  "a.body():\n"
                  "    %ap = ptradd %a, %i\n"
                  "    store i8 1, %ap, align 1, etype i8\n"
                  "    %an = iadd i64 %i, 2\n"
                  "    br a.h(i64 %an)\n"
                  "middle():\n"
                  "    br b.h(i64 0)\n"
                  "b.h(i64 %j):\n"
                  "    %bc = icmp ult i64 %j, %n\n"
                  "    condbr %bc, b.body(), exit()\n"
                  "b.body():\n"
                  "    %bp = ptradd %b, %j\n"
                  "    store i8 2, %bp, align 1, etype i8\n"
                  "    %bn = iadd i64 %j, 2\n"
                  "    br b.h(i64 %bn)\n"
                  "exit():\n"
                  "    ret\n"
                  "}\n");
    T_ASSERT(t, m != NULL);
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O3);
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_fusion(m, &cfg));
    fflush(report);
    rewind(report);
    n = fread(text, 1, sizeof(text) - 1, report);
    text[n] = '\0';
    T_ASSERT(t, strstr(text, "fuse_trip_mismatch") != NULL);
    if (m) {
        T_ASSERT(t, ir_verify(fix.dc, m));
        T_ASSERT_EQ_INT(t, count_op(&m->funcs[0], IR_CONDBR), 2);
    }
    fclose(report);
    arena_free_all(&fix.arena);
}
