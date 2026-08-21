#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "opt/alias.h"
#include "unit.h"
#include "util/arena.h"
#include "util/buf.h"

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

static AliasCtx *alias_ctx_seeds(IrModule *m, const AliasAllocSeed *seeds,
                                 u32 nseeds)
{
    AliasConfig cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.func = &m->funcs[0];
    cfg.alloc_seeds = seeds;
    cfg.nalloc_seeds = nseeds;
    return alias_build(m, &cfg);
}

static AliasCtx *alias_ctx_mixed_seeds(IrModule *m,
                                       const AliasAllocSeed *alloc_seeds,
                                       u32 nalloc_seeds,
                                       const AliasReturnSeed *return_seeds,
                                       u32 nreturn_seeds)
{
    AliasConfig cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.func = &m->funcs[0];
    cfg.alloc_seeds = alloc_seeds;
    cfg.nalloc_seeds = nalloc_seeds;
    cfg.return_seeds = return_seeds;
    cfg.nreturn_seeds = nreturn_seeds;
    return alias_build(m, &cfg);
}

static AliasCtx *
alias_ctx_return_seeds(IrModule *m, const AliasReturnSeed *seeds, u32 nseeds)
{
    AliasConfig cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.func = &m->funcs[0];
    cfg.return_seeds = seeds;
    cfg.nreturn_seeds = nseeds;
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

void test_alias_return_seeds_union_multiple_arguments(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *a, *b, *call;
    AliasReturnSeed seeds[2];
    PtsSet result;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @choose\n"
                        "func ptr @f() {\n"
                        "entry():\n"
                        "    %a = alloca 8, align 8\n"
                        "    %b = alloca 8, align 8\n"
                        "    %p = call ptr @choose(ptr %a, ptr %b)\n"
                        "    ret ptr %p\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    a = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    b = alias_find(&m->funcs[0], IR_ALLOCA, 1);
    call = alias_find(&m->funcs[0], IR_CALL, 0);
    seeds[0] = (AliasReturnSeed){call, 0};
    seeds[1] = (AliasReturnSeed){call, 1};
    c = alias_ctx_return_seeds(m, seeds, 2);
    result = alias_points_to(c, ir_op_value(&m->funcs[0], call->result));
    T_ASSERT(t, !result.has_unknown);
    T_ASSERT_EQ_INT(t, pts_count(result), 2);
    T_ASSERT_EQ_INT(
        t,
        alias_query(c,
                    alias_memloc(c, ir_op_value(&m->funcs[0], call->result), 1,
                                 ETYPE_CHAR),
                    alias_memloc(c, ir_op_value(&m->funcs[0], a->result), 1,
                                 ETYPE_CHAR)),
        ALIAS_MAY);
    T_ASSERT_EQ_INT(
        t,
        alias_query(c,
                    alias_memloc(c, ir_op_value(&m->funcs[0], call->result), 1,
                                 ETYPE_CHAR),
                    alias_memloc(c, ir_op_value(&m->funcs[0], b->result), 1,
                                 ETYPE_CHAR)),
        ALIAS_MAY);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_owned_result_preserves_return_alias_alternatives(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *arg, *call;
    AliasAllocSeed alloc_seed;
    AliasReturnSeed return_seed;
    const AllocSite *fresh;
    PtsSet result;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @fresh_or_alias\n"
                        "func ptr @f() {\n"
                        "entry():\n"
                        "    %arg = alloca 8, align 8\n"
                        "    %p = call ptr @fresh_or_alias(ptr %arg)\n"
                        "    ret ptr %p\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    arg = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    call = alias_find(&m->funcs[0], IR_CALL, 0);
    alloc_seed = (AliasAllocSeed){call, true, ALIAS_NO_OUT_PARAM};
    return_seed = (AliasReturnSeed){call, 0};
    c = alias_ctx_mixed_seeds(m, &alloc_seed, 1, &return_seed, 1);
    fresh = alias_alloc_site(c, call);
    result = alias_points_to(c, ir_op_value(&m->funcs[0], call->result));
    T_ASSERT(t, fresh != NULL);
    T_ASSERT(t, !result.has_unknown);
    T_ASSERT_EQ_INT(t, pts_count(result), 2);
    T_ASSERT(t, alias_pts_has_alloc_site(c, result, fresh));
    T_ASSERT_EQ_INT(
        t,
        alias_query(c,
                    alias_memloc(c, ir_op_value(&m->funcs[0], call->result), 1,
                                 ETYPE_CHAR),
                    alias_memloc(c, ir_op_value(&m->funcs[0], arg->result), 1,
                                 ETYPE_CHAR)),
        ALIAS_MAY);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_allocation_sites_seed_direct_results_in_order(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c, *c2;
    IrInst *first, *plain, *second;
    AliasAllocSeed seeds[2];
    const AllocSite *site0, *site1;
    PtsSet pts;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @fresh_a\n"
                        "sym @borrowed\n"
                        "sym @fresh_b\n"
                        "func void @f() {\n"
                        "entry():\n"
                        "    %a = call ptr @fresh_a(i64 8)\n"
                        "    %p = call ptr @borrowed()\n"
                        "    %b = call ptr @fresh_b(i64 16)\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    first = alias_find(&m->funcs[0], IR_CALL, 0);
    plain = alias_find(&m->funcs[0], IR_CALL, 1);
    second = alias_find(&m->funcs[0], IR_CALL, 2);
    seeds[0] = (AliasAllocSeed){first, true, ALIAS_NO_OUT_PARAM};
    seeds[1] = (AliasAllocSeed){second, true, ALIAS_NO_OUT_PARAM};
    c = alias_ctx_seeds(m, seeds, 2);
    site0 = alias_alloc_site_at(c, 0);
    site1 = alias_alloc_site_at(c, 1);
    T_ASSERT_EQ_INT(t, alias_alloc_site_count(c), 2);
    T_ASSERT(t, site0 != NULL && site1 != NULL && site0 != site1);
    T_ASSERT(t, alias_alloc_site(c, first) == site0);
    T_ASSERT(t, alias_alloc_site(c, second) == site1);
    T_ASSERT(t, alias_alloc_site(c, plain) == NULL);
    T_ASSERT_EQ_INT(t, alias_alloc_site_id(site0), 1);
    T_ASSERT_EQ_INT(t, alias_alloc_site_id(site1), 2);
    T_ASSERT_EQ_INT(t, alias_alloc_site_id(NULL), 0);
    T_ASSERT(t, alias_alloc_site_call(site0) == first);
    T_ASSERT(t, alias_alloc_site_call(site1) == second);
    pts = alias_points_to(c, ir_op_value(&m->funcs[0], first->result));
    T_ASSERT(t, !pts.has_unknown);
    T_ASSERT(t, alias_pts_has_alloc_site(c, pts, site0));
    T_ASSERT(t, !alias_pts_has_alloc_site(c, pts, site1));
    T_ASSERT(t, alias_pts_unique_alloc_site(c, pts) == site0);
    T_ASSERT_EQ_INT(
        t,
        alias_query(c,
                    alias_memloc(c, ir_op_value(&m->funcs[0], first->result), 8,
                                 ETYPE_CHAR),
                    alias_memloc(c, ir_op_value(&m->funcs[0], second->result),
                                 8, ETYPE_CHAR)),
        ALIAS_NO);
    pts = alias_points_to(c, ir_op_value(&m->funcs[0], plain->result));
    T_ASSERT(t, pts.has_unknown);
    T_ASSERT(t, alias_pts_unique_alloc_site(c, pts) == NULL);
    c2 = alias_ctx_seeds(m, seeds, 2);
    T_ASSERT_EQ_INT(t, alias_alloc_site_id(alias_alloc_site_at(c2, 0)), 1);
    T_ASSERT_EQ_INT(t, alias_alloc_site_id(alias_alloc_site_at(c2, 1)), 2);
    T_ASSERT(
        t, pts_equal(
               alias_points_to(c, ir_op_value(&m->funcs[0], first->result)),
               alias_points_to(c2, ir_op_value(&m->funcs[0], first->result))));
    alias_free(c2);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_allocation_site_seeds_out_parameter_contents(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *call, *load;
    AliasAllocSeed seed;
    const AllocSite *site;
    PtsSet pts;
    i64 lo = -1, hi = -1;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @make_out\n"
                        "func ptr @f() {\n"
                        "entry():\n"
                        "    %slot = alloca 8, align 8\n"
                        "    %rc = call i32 @make_out(i64 8, ptr %slot)\n"
                        "    %p = load ptr, %slot, align 8, etype ptr\n"
                        "    ret ptr %p\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    call = alias_find(&m->funcs[0], IR_CALL, 0);
    load = alias_find(&m->funcs[0], IR_LOAD, 0);
    seed = (AliasAllocSeed){call, false, 1};
    c = alias_ctx_seeds(m, &seed, 1);
    site = alias_alloc_site(c, call);
    pts = alias_points_to(c, ir_op_value(&m->funcs[0], load->result));
    T_ASSERT(t, site != NULL);
    T_ASSERT(t, alias_pts_has_alloc_site(c, pts, site));
    /* The allocator's declared out-parameter publication is exact.  Treating
     * the allocator itself as an arbitrary clobber would erase the ownership
     * fact before the memory-safety client could consume it. */
    T_ASSERT(t, !pts.has_unknown);
    T_ASSERT(t, alias_pts_unique_alloc_site(c, pts) == site);
    T_ASSERT(t, alias_offset_range(c, ir_op_value(&m->funcs[0], load->result),
                                   &lo, &hi));
    T_ASSERT_EQ_INT(t, lo, 0);
    T_ASSERT_EQ_INT(t, hi, 0);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_out_parameter_may_target_keeps_unknown_alternative(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *call, *load_a, *load_b;
    AliasAllocSeed seed;
    const AllocSite *site;
    PtsSet a, b;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @make_out\n"
                        "func void @f(i32 %cond) {\n"
                        "entry():\n"
                        "    %a = alloca 8, align 8\n"
                        "    %b = alloca 8, align 8\n"
                        "    %dst = select %cond, ptr %a, %b\n"
                        "    %rc = call i32 @make_out(ptr %dst)\n"
                        "    %pa = load ptr, %a, align 8, etype ptr\n"
                        "    %pb = load ptr, %b, align 8, etype ptr\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    call = alias_find(&m->funcs[0], IR_CALL, 0);
    load_a = alias_find(&m->funcs[0], IR_LOAD, 0);
    load_b = alias_find(&m->funcs[0], IR_LOAD, 1);
    seed = (AliasAllocSeed){call, false, 0};
    c = alias_ctx_seeds(m, &seed, 1);
    site = alias_alloc_site(c, call);
    a = alias_points_to(c, ir_op_value(&m->funcs[0], load_a->result));
    b = alias_points_to(c, ir_op_value(&m->funcs[0], load_b->result));
    T_ASSERT(t, a.has_unknown && b.has_unknown);
    T_ASSERT(t, alias_pts_has_alloc_site(c, a, site));
    T_ASSERT(t, alias_pts_has_alloc_site(c, b, site));
    T_ASSERT(t, alias_pts_unique_alloc_site(c, a) == NULL);
    T_ASSERT(t, alias_pts_unique_alloc_site(c, b) == NULL);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_reachable_allocation_sites_follow_stored_contents(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *first, *second, *root;
    AliasAllocSeed seeds[2];
    const AllocSite *sites[2] = {NULL, NULL};
    bool unknown = true;
    u32 count;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @fresh_a\n"
                        "sym @fresh_b\n"
                        "func void @f() {\n"
                        "entry():\n"
                        "    %a = call ptr @fresh_a()\n"
                        "    %b = call ptr @fresh_b()\n"
                        "    %root = alloca 8, align 8\n"
                        "    store ptr %a, %root, align 8, etype ptr\n"
                        "    store ptr %b, %a, align 8, etype ptr\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    first = alias_find(&m->funcs[0], IR_CALL, 0);
    second = alias_find(&m->funcs[0], IR_CALL, 1);
    root = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    seeds[0] = (AliasAllocSeed){first, true, ALIAS_NO_OUT_PARAM};
    seeds[1] = (AliasAllocSeed){second, true, ALIAS_NO_OUT_PARAM};
    c = alias_ctx_seeds(m, seeds, 2);
    count = alias_reachable_alloc_sites(
        c, ir_op_value(&m->funcs[0], root->result), sites, 1, &unknown);
    T_ASSERT_EQ_INT(t, count, 2);
    T_ASSERT(t, !unknown);
    T_ASSERT(t, sites[0] == alias_alloc_site(c, first));
    T_ASSERT(t, sites[1] == NULL);
    memset(sites, 0, sizeof(sites));
    count = alias_reachable_alloc_sites(
        c, ir_op_value(&m->funcs[0], root->result), sites, 2, &unknown);
    T_ASSERT_EQ_INT(t, count, 2);
    T_ASSERT(t, sites[0] == alias_alloc_site(c, first));
    T_ASSERT(t, sites[1] == alias_alloc_site(c, second));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_unknown_call_clobbers_passed_local_storage(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *call, *load;
    AliasAllocSeed seed;
    PtsSet pts;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @make_out\n"
                        "sym @consume_slot\n"
                        "func ptr @f() {\n"
                        "entry():\n"
                        "    %slot = alloca 8, align 8\n"
                        "    %rc = call i32 @make_out(i64 8, ptr %slot)\n"
                        "    call void @consume_slot(ptr %slot)\n"
                        "    %p = load ptr, %slot, align 8, etype ptr\n"
                        "    ret ptr %p\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    call = alias_find(&m->funcs[0], IR_CALL, 0);
    load = alias_find(&m->funcs[0], IR_LOAD, 0);
    seed = (AliasAllocSeed){call, false, 1};
    c = alias_ctx_seeds(m, &seed, 1);
    pts = alias_points_to(c, ir_op_value(&m->funcs[0], load->result));
    T_ASSERT(t, pts.has_unknown);
    T_ASSERT(t, alias_pts_unique_alloc_site(c, pts) == NULL);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_unknown_call_clobber_follows_global_reachability(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *load;
    PtsSet pts;

    alias_fix_init(&f);
    m = alias_parse(&f, "global @root size 8 align 8 external\n"
                        "sym @unknown\n"
                        "func ptr @f() {\n"
                        "entry():\n"
                        "    %object = alloca 8, align 8\n"
                        "    %slot = alloca 8, align 8\n"
                        "    store ptr %object, %slot, align 8, etype ptr\n"
                        "    store ptr %slot, @root, align 8, etype ptr\n"
                        "    call void @unknown()\n"
                        "    %p = load ptr, %slot, align 8, etype ptr\n"
                        "    ret ptr %p\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    load = alias_find(&m->funcs[0], IR_LOAD, 0);
    pts = alias_points_to(c, ir_op_value(&m->funcs[0], load->result));
    T_ASSERT(t, pts.has_unknown);
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_unknown_call_preserves_unreachable_local_storage(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *object, *load;
    PtsSet object_pts, loaded_pts;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @unknown\n"
                        "func ptr @f() {\n"
                        "entry():\n"
                        "    %object = alloca 8, align 8\n"
                        "    %slot = alloca 8, align 8\n"
                        "    store ptr %object, %slot, align 8, etype ptr\n"
                        "    call void @unknown()\n"
                        "    %p = load ptr, %slot, align 8, etype ptr\n"
                        "    ret ptr %p\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    object = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    load = alias_find(&m->funcs[0], IR_LOAD, 0);
    object_pts = alias_points_to(c, ir_op_value(&m->funcs[0], object->result));
    loaded_pts = alias_points_to(c, ir_op_value(&m->funcs[0], load->result));
    T_ASSERT(t, !loaded_pts.has_unknown);
    T_ASSERT(t, pts_equal(object_pts, loaded_pts));
    alias_free(c);
    arena_free_all(&f.arena);
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
    i64 lo = 0, hi = 0;

    alias_fix_init(&f);
    m = alias_parse(&f, "func ptr @f() {\n"
                        "entry():\n"
                        "    %obj = alloca 8, align 8\n"
                        "    %at = ptradd %obj, 12\n"
                        "    %slot = alloca 8, align 8\n"
                        "    store ptr %at, %slot, align 8, etype ptr\n"
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
    T_ASSERT(t, alias_offset_range(c, ir_op_value(&m->funcs[0], load->result),
                                   &lo, &hi));
    T_ASSERT_EQ_INT(t, lo, 12);
    T_ASSERT_EQ_INT(t, hi, 12);
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

void test_alias_unknown_effective_type_suppresses_tbaa(TestCtx *t)
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
                    alias_query(c, alias_memloc(c, p, 4, ETYPE_UNKNOWN),
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

void test_alias_nonheap_proof_accepts_stack_and_global_objects(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *stack;
    IrOperand global;

    alias_fix_init(&f);
    m = alias_parse(&f, "global @g size 8 align 8 external\n"
                        "func void @f() {\n"
                        "entry():\n"
                        "    %stack = alloca 8, align 8\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    stack = alias_find(&m->funcs[0], IR_ALLOCA, 0);
    global = ir_op_symbol(IRT_PTR, ir_sym(m, "g"), 0);
    T_ASSERT(t, alias_pts_must_be_nonheap(
                    c, alias_points_to(
                           c, ir_op_value(&m->funcs[0], stack->result))));
    T_ASSERT(t, alias_pts_must_be_nonheap(c, alias_points_to(c, global)));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_nonheap_proof_rejects_unknown_parameter(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrOperand param;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f(ptr %p) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    param = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[0]);
    T_ASSERT(t, !alias_pts_must_be_nonheap(c, alias_points_to(c, param)));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_nonheap_proof_rejects_restrict_parameter(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrOperand param;

    alias_fix_init(&f);
    m = alias_parse(&f, "func void @f(ptr restrict %p) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    param = ir_op_value(&m->funcs[0], m->funcs[0].param_vals[0]);
    T_ASSERT(t, !alias_pts_must_be_nonheap(c, alias_points_to(c, param)));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_nonheap_proof_rejects_seeded_heap_site(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *call;
    AliasAllocSeed seed;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @fresh\n"
                        "func void @f() {\n"
                        "entry():\n"
                        "    %p = call ptr @fresh(i64 8)\n"
                        "    ret\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    call = alias_find(&m->funcs[0], IR_CALL, 0);
    seed = (AliasAllocSeed){call, true, ALIAS_NO_OUT_PARAM};
    c = alias_ctx_seeds(m, &seed, 1);
    T_ASSERT(
        t, !alias_pts_must_be_nonheap(
               c, alias_points_to(c, ir_op_value(&m->funcs[0], call->result))));
    alias_free(c);
    arena_free_all(&f.arena);
}

void test_alias_domain_omits_unreferenced_module_symbols(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    Buf src;
    PtsSet used, unused;
    u32 i;

    alias_fix_init(&f);
    buf_init(&src);
    buf_printf(&src, "global @used size 8 align 8 external\n");
    for (i = 0; i < 128; i++)
        buf_printf(&src, "global @unused_%u size 8 align 8 external\n", i);
    buf_printf(&src, "func ptr @f() {\n"
                     "entry():\n"
                     "    ret ptr @used\n"
                     "}\n");
    buf_push_u8(&src, 0);
    m = alias_parse(&f, (const char *)src.data);
    T_ASSERT(t, m != NULL);
    if (!m) {
        buf_free(&src);
        arena_free_all(&f.arena);
        return;
    }
    c = alias_ctx(m, false);
    used = alias_points_to(c, ir_op_symbol(IRT_PTR, ir_sym(m, "used"), 0));
    unused =
        alias_points_to(c, ir_op_symbol(IRT_PTR, ir_sym(m, "unused_0"), 0));
    T_ASSERT_EQ_INT(t, used.nwords, 1);
    T_ASSERT_EQ_INT(t, pts_count(used), 1);
    T_ASSERT(t, !used.has_unknown);
    T_ASSERT_EQ_INT(t, unused.nwords, 1);
    T_ASSERT_EQ_INT(t, pts_count(unused), 1);
    T_ASSERT(t, !unused.has_unknown);
    T_ASSERT_EQ_INT(
        t,
        alias_query(
            c,
            alias_memloc(c, ir_op_symbol(IRT_PTR, ir_sym(m, "unused_0"), 0), 8,
                         ETYPE_I64),
            alias_memloc(c, ir_op_symbol(IRT_PTR, ir_sym(m, "unused_1"), 0), 8,
                         ETYPE_I64)),
        ALIAS_NO);
    alias_free(c);
    buf_free(&src);
    arena_free_all(&f.arena);
}

void test_alias_runtime_index_guard_preserves_heap_pointer_contents(TestCtx *t)
{
    AliasFix f;
    IrModule *m;
    AliasCtx *c;
    IrInst *slot_call, *pointer_call, *load;
    AliasAllocSeed seeds[2];
    const AllocSite *pointer_site;
    PtsSet loaded;

    alias_fix_init(&f);
    m = alias_parse(&f, "sym @malloc\n"
                        "sym @cgf_safe_check_index\n"
                        "func ptr @f(i64 %index) {\n"
                        "entry():\n"
                        "    %slot = call ptr @malloc(i64 8)\n"
                        "    %p = call ptr @malloc(i64 16)\n"
                        "    store ptr %p, %slot, align 8, etype ptr\n"
                        "    %off = imul i64 %index, 1\n"
                        "    %q = ptradd %slot, %off\n"
                        "    call void @cgf_safe_check_index(ptr %slot, i64 "
                        "%index, i64 1, i32 0, ptr %q, i32 1)\n"
                        "    %reload = load ptr, %slot, align 8, etype ptr\n"
                        "    ret ptr %reload\n"
                        "}\n");
    T_ASSERT(t, m != NULL);
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    slot_call = alias_find(&m->funcs[0], IR_CALL, 0);
    pointer_call = alias_find(&m->funcs[0], IR_CALL, 1);
    load = alias_find(&m->funcs[0], IR_LOAD, 0);
    seeds[0] = (AliasAllocSeed){slot_call, true, ALIAS_NO_OUT_PARAM};
    seeds[1] = (AliasAllocSeed){pointer_call, true, ALIAS_NO_OUT_PARAM};
    c = alias_ctx_seeds(m, seeds, 2);
    pointer_site = alias_alloc_site(c, pointer_call);
    loaded = alias_points_to(c, ir_op_value(&m->funcs[0], load->result));
    T_ASSERT(t, pointer_site != NULL);
    T_ASSERT(t, !loaded.has_unknown);
    T_ASSERT(t, alias_pts_unique_alloc_site(c, loaded) == pointer_site);
    T_ASSERT(t,
             !alias_escapes(c, ir_op_value(&m->funcs[0], slot_call->result)));
    alias_free(c);
    arena_free_all(&f.arena);
}
