#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "opt/alias.h"
#include "unit.h"
#include "util/arena.h"

typedef struct {
    Arena arena;
    DiagCtx *dc;
} AliasFix;

static void alias_silent_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void alias_fix_init(AliasFix *f)
{
    DiagSink sink = {alias_silent_sink, NULL};

    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
}

static IrModule *alias_parse(AliasFix *f, const char *src)
{
    IrModule *m = ir_parse_module(&f->arena, f->dc, src, "<alias-test>");

    if (m && !ir_verify(f->dc, m))
        return NULL;
    return m;
}

static AliasCtx *alias_ctx(IrModule *m, bool no_strict)
{
    AliasConfig cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.func = &m->funcs[0];
    cfg.no_strict_aliasing = no_strict;
    return alias_build(m, &cfg);
}

static IrInst *alias_find(IrFunc *f, IrOp op, u32 ordinal)
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

static u32 pts_count(PtsSet s)
{
    u32 wi, n = 0;

    for (wi = 0; wi < s.nwords; wi++) {
        u64 x = s.words[wi];

        while (x) {
            x &= x - 1;
            n++;
        }
    }
    return n;
}

static bool pts_equal(PtsSet a, PtsSet b)
{
    return a.nwords == b.nwords &&
           memcmp(a.words, b.words, a.nwords * sizeof(u64)) == 0;
}

void test_alias_addr_of_alloca_and_global_constraints(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *a;
    IrOperand ap, gp;

    alias_fix_init(&f);
    m = alias_parse(&f, "global @g size 8 align 8 external\n"
                        "func void @f() {\n"
                        "entry():\n"
                        "    %a = alloca 8, align 8, etype i64\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    a = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    ap = ir_op_value(&m->funcs[0], a->result);
    gp = ir_op_symbol(IRT_PTR, ir_sym(m, "g"), 0);
    T_ASSERT_EQ_INT(t, pts_count(alias_points_to(c, ap)), 1);
    T_ASSERT_EQ_INT(t, pts_count(alias_points_to(c, gp)), 1);
    T_ASSERT_EQ_INT(t,
                    alias_query(c, alias_memloc(c, ap, 8, ETYPE_I64),
                                alias_memloc(c, gp, 8, ETYPE_I64)),
                    ALIAS_NO);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_select_and_bitcast_copy_constraints(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *a, *b, *bc, *sel;
    PtsSet ap, bcp, sp;

    alias_fix_init(&f);
    m = alias_parse(&f, "func ptr @f(i32 %c) {\n"
                        "entry():\n"
                        "    %a = alloca 8, align 8\n"
                        "    %b = alloca 8, align 8\n"
                        "    %bits = bitcast ptr %a to i64\n"
                        "    %bc = bitcast i64 %bits to ptr\n"
                        "    %s = select %c, ptr %bc, %b\n"
                        "    ret ptr %s\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    a = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    b = alias_find(&m->funcs[0], IR_ALLOCA, 1);
    bc = alias_find(&m->funcs[0], IR_BITCAST, 1);
    sel = alias_find(&m->funcs[0], IR_SELECT, 0);
    ap = alias_points_to(c, ir_op_value(&m->funcs[0], a->result));
    bcp = alias_points_to(c, ir_op_value(&m->funcs[0], bc->result));
    sp = alias_points_to(c, ir_op_value(&m->funcs[0], sel->result));
    T_ASSERT(t, pts_equal(ap, bcp));
    T_ASSERT_EQ_INT(t, pts_count(sp), 2);
    T_ASSERT(t, !pts_equal(sp, alias_points_to(
                                   c, ir_op_value(&m->funcs[0], b->result))));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_block_parameter_copy_constraint(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *a;
    ValueId p;

    alias_fix_init(&f);
    m = alias_parse(&f, "func ptr @f() {\n"
                        "entry():\n"
                        "    %a = alloca 8, align 8\n"
                        "    br join(ptr %a)\n"
                        "join(ptr %p):\n"
                        "    ret ptr %p\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    a = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    p = m->funcs[0].blocks[1].params[0];
    T_ASSERT(t,
             pts_equal(alias_points_to(c, ir_op_value(&m->funcs[0], a->result)),
                       alias_points_to(c, ir_op_value(&m->funcs[0], p))));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_load_and_store_constraints(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *obj, *load;

    alias_fix_init(&f);
    m = alias_parse(&f, "func ptr @f() {\n"
                        "entry():\n"
                        "    %obj = alloca 8, align 8\n"
                        "    %slot = alloca 8, align 8\n"
                        "    store ptr %obj, %slot, align 8, etype ptr\n"
                        "    %p = load ptr, %slot, align 8, etype ptr\n"
                        "    ret ptr %p\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    obj = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    load = alias_find(&m->funcs[0], IR_LOAD, 0);
    T_ASSERT(
        t,
        pts_equal(alias_points_to(c, ir_op_value(&m->funcs[0], obj->result)),
                  alias_points_to(c, ir_op_value(&m->funcs[0], load->result))));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_ptradd_constant_and_unknown_offset_constraints(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *a, *known, *unknown;
    i64 lo = 0, hi = 0;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f(i64 %n) {\n"
                        "entry():\n"
                        "    %a = alloca 32, align 8\n"
                        "    %k = ptradd %a, 4\n"
                        "    %u = ptradd %a, %n\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    a = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    known = alias_find(&m->funcs[0], IR_PTRADD, 0);
    unknown = alias_find(&m->funcs[0], IR_PTRADD, 1);
    T_ASSERT(t,
             pts_equal(
                 alias_points_to(c, ir_op_value(&m->funcs[0], a->result)),
                 alias_points_to(c, ir_op_value(&m->funcs[0], known->result))));
    T_ASSERT(t, alias_offset_range(c, ir_op_value(&m->funcs[0], known->result),
                                   &lo, &hi));
    T_ASSERT_EQ_INT(t, lo, 4);
    T_ASSERT_EQ_INT(t, hi, 4);
    T_ASSERT(t, !alias_offset_range(
                    c, ir_op_value(&m->funcs[0], unknown->result), &lo, &hi));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_exact_partial_and_adjacent_ranges(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *a;
    IrOperand ap;
    MemLoc x, exact, partial, adjacent, outer;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f() {\n"
                        "entry():\n"
                        "    %a = alloca 16, align 8\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    a = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    ap = ir_op_value(&m->funcs[0], a->result);
    x = alias_memloc(c, ap, 4, ETYPE_I32);
    exact = x;
    partial = x;
    partial.off_lo = partial.off_hi = 2;
    adjacent = x;
    adjacent.off_lo = adjacent.off_hi = 4;
    outer = alias_memloc(c, ap, 16, ETYPE_CHAR);
    T_ASSERT_EQ_INT(t, alias_query(c, x, exact), ALIAS_MUST);
    T_ASSERT_EQ_INT(t, alias_query(c, x, partial), ALIAS_MAY);
    T_ASSERT_EQ_INT(t, alias_query(c, x, adjacent), ALIAS_NO);
    T_ASSERT(t, alias_covers(c, outer, x));
    T_ASSERT(t, !alias_covers(c, x, outer));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_char_wildcard_precedes_type_disjointness(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrOperand p, q;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f(ptr %p, ptr %q) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    p = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[0]);
    q = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[1]);
    T_ASSERT_EQ_INT(t,
                    alias_query(c, alias_memloc(c, p, 4, ETYPE_CHAR),
                                alias_memloc(c, q, 4, ETYPE_F32)),
                    ALIAS_MAY);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_union_blob_never_uses_type_noalias(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrOperand p, q;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f(ptr %p, ptr %q) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    p = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[0]);
    q = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[1]);
    T_ASSERT_EQ_INT(t,
                    alias_query(c, alias_memloc(c, p, 4, ETYPE_UNION),
                                alias_memloc(c, q, 4, ETYPE_F32)),
                    ALIAS_MAY);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_signed_unsigned_width_class_is_compatible(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrOperand p, q;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f(ptr %p, ptr %q) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    p = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[0]);
    q = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[1]);
    /* Both signed int and unsigned int lower to the same ETYPE_I32 class. */
    T_ASSERT_EQ_INT(t,
                    alias_query(c, alias_memloc(c, p, 4, ETYPE_I32),
                                alias_memloc(c, q, 4, ETYPE_I32)),
                    ALIAS_MAY);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_distinct_restrict_parameters_do_not_alias(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrOperand p, q;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f(ptr restrict %p, ptr restrict %q) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    p = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[0]);
    q = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[1]);
    T_ASSERT_EQ_INT(t,
                    alias_query(c, alias_memloc(c, p, 4, ETYPE_I32),
                                alias_memloc(c, q, 4, ETYPE_I32)),
                    ALIAS_NO);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_no_strict_drops_only_type_proofs(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *strict, *loose;
    IrOperand p, q;
    MemLoc pi, qf, adjacent;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f(ptr %p, ptr %q) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    strict = alias_ctx(m, false);
    loose = alias_ctx(m, true);
    p = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[0]);
    q = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[1]);
    pi = alias_memloc(strict, p, 4, ETYPE_I32);
    qf = alias_memloc(strict, q, 4, ETYPE_F32);
    T_ASSERT_EQ_INT(t, alias_query(strict, pi, qf), ALIAS_NO);
    pi = alias_memloc(loose, p, 4, ETYPE_I32);
    qf = alias_memloc(loose, q, 4, ETYPE_F32);
    T_ASSERT_EQ_INT(t, alias_query(loose, pi, qf), ALIAS_MAY);
    adjacent = pi;
    adjacent.off_lo = adjacent.off_hi = 4;
    T_ASSERT_EQ_INT(t, alias_query(loose, pi, adjacent), ALIAS_NO);
    alias_free(strict);
    alias_free(loose);
    arena_free_all(&f.arena);
}

void test_alias_escape_sources_and_local_nonescape(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *local, *stored, *called;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @sink\n"
                        "func ptr @f() {\n"
                        "entry():\n"
                        "    %local = alloca 8, align 8\n"
                        "    %stored = alloca 8, align 8\n"
                        "    %slot = alloca 8, align 8\n"
                        "    %called = alloca 8, align 8\n"
                        "    store ptr %stored, %slot, align 8, etype ptr\n"
                        "    call void @sink(ptr %called)\n"
                        "    ret ptr %slot\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    local = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    stored = alias_find(&m->funcs[0], IR_ALLOCA, 1);
    called = alias_find(&m->funcs[0], IR_ALLOCA, 3);
    T_ASSERT(t, !alias_escapes(c, ir_op_value(&m->funcs[0], local->result)));
    T_ASSERT(t, alias_escapes(c, ir_op_value(&m->funcs[0], stored->result)));
    T_ASSERT(t, alias_escapes(c, ir_op_value(&m->funcs[0], called->result)));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_offset_overflow_widens_to_unknown(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *p;
    i64 lo, hi;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f() {\n"
                        "entry():\n"
                        "    %a = alloca 8, align 8\n"
                        "    %p = ptradd %a, 9223372036854775807\n"
                        "    %q = ptradd %p, 1\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    p = alias_find(&m->funcs[0], IR_PTRADD, 1);
    T_ASSERT(t, !alias_offset_range(c, ir_op_value(&m->funcs[0], p->result),
                                    &lo, &hi));
    alias_free(c);
    arena_free_all(&f.arena);
}
