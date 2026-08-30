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

static u32 func_internal_calls_to(const IrFunc *f, u32 callee)
{
    u32 bi, n = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            n += in->op == IR_CALL && in->subop == FUNCREF_INTERNAL &&
                 in->callee == callee;
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

static bool unlink_inst(IrFunc *f, IrInst *needle)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *b = &f->blocks[bi];
        IrInst *prev = NULL;
        IrInst *in;

        for (in = b->first; in; prev = in, in = in->next)
            if (in == needle) {
                if (prev)
                    prev->next = in->next;
                else
                    b->first = in->next;
                if (b->last == in)
                    b->last = prev;
                b->ninsts--;
                in->next = NULL;
                return true;
            }
    }
    return false;
}

static bool insert_after(IrFunc *f, IrInst *after, IrInst *in)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *b = &f->blocks[bi];
        IrInst *at;

        for (at = b->first; at; at = at->next)
            if (at == after) {
                in->next = at->next;
                at->next = in;
                if (b->last == at)
                    b->last = in;
                b->ninsts++;
                return true;
            }
    }
    return false;
}

static bool insert_before(IrFunc *f, IrInst *before, IrInst *in)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *b = &f->blocks[bi];
        IrInst *prev = NULL;
        IrInst *at;

        for (at = b->first; at; prev = at, at = at->next)
            if (at == before) {
                in->next = at;
                if (prev)
                    prev->next = in;
                else
                    b->first = in;
                b->ninsts++;
                return true;
            }
    }
    return false;
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

void test_opt_inline_multiblock_suffix_preserves_pinned_cfg_order(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    const Pass *passes[] = {&OPT_PASS_INLINE};

    inline_fix_init(&f);
    m = inline_parse(&f, "global @g size 4 align 4 external\n"
                         "func i32 @leaf(i32 %x) internal {\n"
                         "entry():\n"
                         "    %y = iadd i32 %x, 1\n"
                         "    ret i32 %y\n"
                         "}\n"
                         "func void @caller(i32 %x) {\n"
                         "entry():\n"
                         "    store i32 1, @g, align 4, volatile, etype i32\n"
                         "    %y = call i32 @leaf(i32 %x)\n"
                         "    store i32 2, @g, align 4, volatile, etype i32\n"
                         "    %c = icmp ne i32 %y, 0\n"
                         "    condbr %c, yes(), no()\n"
                         "yes():\n"
                         "    store i32 3, @g, align 4, volatile, etype i32\n"
                         "    ret\n"
                         "no():\n"
                         "    ret\n"
                         "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    cfg.verify_after_each = true;
    T_ASSERT(t, m && opt_run_pass_sequence(m, &cfg, passes, 1));
    if (m) {
        T_ASSERT(t, ir_verify(f.dc, m));
        T_ASSERT_EQ_INT(t, func_internal_calls_to(&m->funcs[1], 0), 0);
        T_ASSERT_EQ_INT(t, ir_count_volatile_ops(&m->funcs[1]), 3);
    }
    arena_free_all(&f.arena);
}

void test_opt_inline_pinned_backward_reversal(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    Arena snapshot_arena;
    IrVolatileSnapshot *snapshot;
    IrInlinePinnedGroup *group;
    IrInst *after_anchor;
    u32 bad = 99;

    inline_fix_init(&f);
    m = inline_parse(&f, "global @g size 4 align 4 external\n"
                         "func void @touch() internal {\n"
                         "entry():\n"
                         "    br late()\n"
                         "early():\n"
                         "    store i32 2, @g, align 4, volatile\n"
                         "    ret\n"
                         "late():\n"
                         "    store i32 1, @g, align 4, volatile\n"
                         "    br early()\n"
                         "}\n"
                         "func void @caller() {\n"
                         "entry():\n"
                         "    store i32 0, @g, align 4, volatile\n"
                         "    call void @touch()\n"
                         "    store i32 3, @g, align 4, volatile\n"
                         "    ret\n"
                         "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    arena_init(&snapshot_arena);
    snapshot = arena_alloc(&snapshot_arena, (m->nfuncs + 1) * sizeof(*snapshot),
                           _Alignof(IrVolatileSnapshot));
    ir_snapshot_volatile_order(&snapshot_arena, m, snapshot);
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    T_ASSERT(t, opt_inline(m, &cfg));
    T_ASSERT_EQ_INT(t, m->ninline_pinned_groups, 1);
    T_ASSERT(t, ir_pinned_inline_matches(m, snapshot, &bad));
    group = &m->inline_pinned_groups[0];
    T_ASSERT_EQ_INT(t, group->nops, 2);
    /* Source op[1] dominates source op[0] despite later document layout. */
    T_ASSERT_EQ_INT(t, group->precedes[2], 1);
    after_anchor = store_of_iconst(&m->funcs[1], 3);
    T_ASSERT(t, after_anchor != NULL);
    T_ASSERT(t, unlink_inst(&m->funcs[1], group->clones[0]));
    T_ASSERT(t, insert_after(&m->funcs[1], after_anchor, group->clones[0]));
    T_ASSERT(t, !ir_pinned_inline_matches(m, snapshot, &bad));
    T_ASSERT_EQ_INT(t, bad, 1);
    T_ASSERT(t, unlink_inst(&m->funcs[1], group->clones[0]));
    T_ASSERT(t, insert_before(&m->funcs[1], after_anchor, group->clones[0]));
    T_ASSERT(t, ir_pinned_inline_matches(m, snapshot, &bad));
    T_ASSERT(t, unlink_inst(&m->funcs[1], group->clones[1]));
    T_ASSERT(t, insert_after(&m->funcs[1], group->clones[0], group->clones[1]));
    T_ASSERT(t, !ir_pinned_inline_matches(m, snapshot, &bad));
    T_ASSERT_EQ_INT(t, bad, 1);
    arena_free_all(&snapshot_arena);
    arena_free_all(&f.arena);
}

void test_opt_inline_pinned_clone_group_rejects_duplicate_omission(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    Arena snapshot_arena;
    IrVolatileSnapshot *snapshot;
    IrInlinePinnedGroup *group;
    IrInst *duplicate;
    u32 bi, bad = 99;
    bool replaced = false;

    inline_fix_init(&f);
    m = inline_parse(&f, "global @g size 4 align 4 external\n"
                         "func void @touch() internal {\n"
                         "entry():\n"
                         "    store i32 1, @g, align 4, volatile\n"
                         "    store i32 2, @g, align 4, volatile\n"
                         "    ret\n"
                         "}\n"
                         "func void @caller() {\n"
                         "entry():\n"
                         "    call void @touch()\n"
                         "    ret\n"
                         "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    arena_init(&snapshot_arena);
    snapshot = arena_alloc(&snapshot_arena, (m->nfuncs + 1) * sizeof(*snapshot),
                           _Alignof(IrVolatileSnapshot));
    ir_snapshot_volatile_order(&snapshot_arena, m, snapshot);
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    T_ASSERT(t, opt_inline(m, &cfg));
    T_ASSERT(t, ir_pinned_inline_matches(m, snapshot, &bad));
    group = &m->inline_pinned_groups[0];
    duplicate = arena_alloc(&f.arena, sizeof(*duplicate), _Alignof(IrInst));
    *duplicate = *group->clones[0];
    for (bi = 0; bi < m->funcs[1].nblocks && !replaced; bi++) {
        IrBlock *b = &m->funcs[1].blocks[bi];
        IrInst *prev = NULL;
        IrInst *in;

        for (in = b->first; in; prev = in, in = in->next)
            if (in == group->clones[1]) {
                duplicate->next = in->next;
                if (prev)
                    prev->next = duplicate;
                else
                    b->first = duplicate;
                if (b->last == in)
                    b->last = duplicate;
                replaced = true;
                break;
            }
    }
    T_ASSERT(t, replaced);
    T_ASSERT(t, !ir_pinned_inline_matches(m, snapshot, &bad));
    T_ASSERT_EQ_INT(t, bad, 1);
    arena_free_all(&snapshot_arena);
    arena_free_all(&f.arena);
}

void test_opt_inline_pinned_clone_cannot_alias_original(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    Arena snapshot_arena;
    IrVolatileSnapshot *snapshot;
    IrInlinePinnedGroup *group;
    IrInst *original, *actual_clone;
    u32 bad = 99;

    inline_fix_init(&f);
    m = inline_parse(&f, "global @g size 4 align 4 external\n"
                         "func void @touch() internal {\n"
                         "entry():\n"
                         "    store i32 1, @g, align 4, volatile\n"
                         "    ret\n"
                         "}\n"
                         "func void @caller() {\n"
                         "entry():\n"
                         "    store i32 0, @g, align 4, volatile\n"
                         "    call void @touch()\n"
                         "    ret\n"
                         "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    original = store_of_iconst(&m->funcs[1], 0);
    arena_init(&snapshot_arena);
    snapshot = arena_alloc(&snapshot_arena, (m->nfuncs + 1) * sizeof(*snapshot),
                           _Alignof(IrVolatileSnapshot));
    ir_snapshot_volatile_order(&snapshot_arena, m, snapshot);
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    T_ASSERT(t, opt_inline(m, &cfg));
    T_ASSERT(t, ir_pinned_inline_matches(m, snapshot, &bad));
    group = &m->inline_pinned_groups[0];
    actual_clone = group->clones[0];
    T_ASSERT(t, original != NULL && actual_clone != NULL);
    T_ASSERT(t, unlink_inst(&m->funcs[1], actual_clone));
    group->clones[0] = original;
    T_ASSERT(t, !ir_pinned_inline_matches(m, snapshot, &bad));
    T_ASSERT_EQ_INT(t, bad, 1);
    arena_free_all(&snapshot_arena);
    arena_free_all(&f.arena);
}

void test_opt_inline_pinned_anchor_set_is_exact(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    Arena snapshot_arena;
    IrVolatileSnapshot *snapshot;
    IrInlinePinnedGroup *group;
    const IrInst *last_anchor;
    u32 saved_nanchors, bad = 99;

    inline_fix_init(&f);
    m = inline_parse(&f, "global @g size 4 align 4 external\n"
                         "func void @touch() internal {\n"
                         "entry():\n"
                         "    store i32 1, @g, align 4, volatile\n"
                         "    ret\n"
                         "}\n"
                         "func void @caller() {\n"
                         "entry():\n"
                         "    store i32 0, @g, align 4, volatile\n"
                         "    call void @touch()\n"
                         "    store i32 3, @g, align 4, volatile\n"
                         "    call void @touch()\n"
                         "    store i32 4, @g, align 4, volatile\n"
                         "    ret\n"
                         "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    arena_init(&snapshot_arena);
    snapshot = arena_alloc(&snapshot_arena, (m->nfuncs + 1) * sizeof(*snapshot),
                           _Alignof(IrVolatileSnapshot));
    ir_snapshot_volatile_order(&snapshot_arena, m, snapshot);
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    T_ASSERT(t, opt_inline(m, &cfg));
    T_ASSERT_EQ_INT(t, m->ninline_pinned_groups, 2);
    T_ASSERT(t, ir_pinned_inline_matches(m, snapshot, &bad));
    group = &m->inline_pinned_groups[1];
    saved_nanchors = group->nanchors;
    T_ASSERT_EQ_INT(t, saved_nanchors, 4);
    /* The second site must name all three snapshot originals and the first
     * site's already-recorded clone, exactly once each. */
    group->nanchors = 0;
    T_ASSERT(t, !ir_pinned_inline_matches(m, snapshot, &bad));
    group->nanchors = saved_nanchors - 1;
    T_ASSERT(t, !ir_pinned_inline_matches(m, snapshot, &bad));
    group->nanchors = saved_nanchors;
    last_anchor = group->anchors[saved_nanchors - 1];
    group->anchors[saved_nanchors - 1] = group->anchors[0];
    T_ASSERT(t, !ir_pinned_inline_matches(m, snapshot, &bad));
    group->anchors[saved_nanchors - 1] = last_anchor;
    T_ASSERT(t, ir_pinned_inline_matches(m, snapshot, &bad));
    arena_free_all(&snapshot_arena);
    arena_free_all(&f.arena);
}

void test_opt_inline_conditional_pinned_does_not_claim_post_anchor(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    Arena snapshot_arena;
    IrVolatileSnapshot *snapshot;
    IrInlinePinnedGroup *group;
    u32 bad = 99;

    inline_fix_init(&f);
    m = inline_parse(&f, "global @g size 4 align 4 external\n"
                         "func void @touch(i32 %c) internal {\n"
                         "entry():\n"
                         "    condbr %c, yes(), no()\n"
                         "yes():\n"
                         "    store i32 1, @g, align 4, volatile\n"
                         "    ret\n"
                         "no():\n"
                         "    ret\n"
                         "}\n"
                         "func void @caller(i32 %c) {\n"
                         "entry():\n"
                         "    call void @touch(i32 %c)\n"
                         "    store i32 2, @g, align 4, volatile\n"
                         "    ret\n"
                         "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    if (!m) {
        arena_free_all(&f.arena);
        return;
    }
    arena_init(&snapshot_arena);
    snapshot = arena_alloc(&snapshot_arena, (m->nfuncs + 1) * sizeof(*snapshot),
                           _Alignof(IrVolatileSnapshot));
    ir_snapshot_volatile_order(&snapshot_arena, m, snapshot);
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    T_ASSERT(t, opt_inline(m, &cfg));
    T_ASSERT(t, ir_pinned_inline_matches(m, snapshot, &bad));
    group = &m->inline_pinned_groups[0];
    T_ASSERT_EQ_INT(t, group->nops, 1);
    T_ASSERT_EQ_INT(t, group->nanchors, 1);
    /* The source access does not dominate both returns, so it must not be
     * falsely required to dominate the caller's post-call anchor. */
    T_ASSERT_EQ_INT(t, group->clone_precedes[0], 0);
    arena_free_all(&snapshot_arena);
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
                "func f80 @choose80(i32 %c) {\n"
                "entry():\n"
                "    condbr %c, yes(), no()\n"
                "yes():\n"
                "    ret f80 0x3FFF:0x8000000000000000\n"
                "no():\n"
                "    ret f80 0x4000:0x8000000000000000\n"
                "}\n"
                "func f80 @caller(i32 %c) {\n"
                "entry():\n"
                "    %x = call f80 @choose80(i32 %c)\n"
                "    ret f80 %x\n"
                "}\n",
                "inl_f80_multiret", false);
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

void test_opt_inline_updates_direct_call_facts_after_each_splice(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    Buf src;

    inline_fix_init(&f);
    buf_init(&src);
    emit_cost_function(&src, "large", 23, true);
    buf_printf(&src, "func i32 @caller(i32 %%c, i32 %%x) {\n"
                     "entry():\n"
                     "    %%first = call i32 @large(i32 1, i32 7)\n"
                     "    %%second = call i32 @large(i32 %%c, i32 %%x)\n"
                     "    %%result = iadd i32 %%first, %%second\n"
                     "    ret i32 %%result\n"
                     "}\n");
    buf_push_u8(&src, 0);
    m = inline_parse(&f, (char *)src.data);
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 20;
    T_ASSERT(t, m && opt_inline(m, &cfg));
    /* The constant control argument admits the first site at threshold 30.
     * Its splice leaves one direct call, so exact delta accounting must apply
     * the internal single-site multiplier and admit the second site at 80. */
    T_ASSERT_EQ_INT(t, m ? func_op_count(&m->funcs[1], IR_CALL) : 0, 0);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    buf_free(&src);
    arena_free_all(&f.arena);
}

static IrModule *growth_boundary_module(InlineFix *f, u32 external_calls)
{
    Buf src;
    IrModule *m;
    u32 i;

    buf_init(&src);
    buf_printf(&src, "sym @sink\n"
                     "func void @leaf() internal {\n"
                     "entry():\n"
                     "    ret\n"
                     "}\n"
                     "func void @caller() {\n"
                     "entry():\n");
    for (i = 0; i < external_calls; i++)
        buf_printf(&src, "    call void @sink()\n");
    buf_printf(&src, "    call void @leaf()\n"
                     "    ret\n"
                     "}\n");
    buf_push_u8(&src, 0);
    m = inline_parse(f, (char *)src.data);
    buf_free(&src);
    return m;
}

void test_opt_inline_growth_budget_exact_boundary_and_determinism(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    FILE *report;
    Buf first, second;
    char log[256];

    inline_fix_init(&f);
    m = growth_boundary_module(&f, 509);
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    T_ASSERT(t, m && opt_inline(m, &cfg));
    T_ASSERT_EQ_INT(t, m ? func_op_count(&m->funcs[1], IR_CALL) : 1, 509);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    arena_free_all(&f.arena);

    inline_fix_init(&f);
    m = growth_boundary_module(&f, 510);
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    report = tmpfile();
    T_ASSERT(t, report != NULL);
    opt_config_init(&cfg, OPT_O2);
    cfg.inline_threshold = 40;
    cfg.bail_log = true;
    cfg.report = report;
    T_ASSERT(t, m && !opt_inline(m, &cfg));
    T_ASSERT_EQ_INT(t, m ? func_op_count(&m->funcs[1], IR_CALL) : 0, 511);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    if (report) {
        read_report(report, log, sizeof(log));
        T_ASSERT(t, strstr(log, "inl_growth_budget") != NULL);
        fclose(report);
    }
    (void)inline_print(m, &first);
    cfg.bail_log = false;
    cfg.report = stderr;
    T_ASSERT(t, !opt_inline(m, &cfg));
    (void)inline_print(m, &second);
    T_ASSERT_EQ_INT(t, first.len, second.len);
    T_ASSERT(t, first.len == second.len &&
                    memcmp(first.data, second.data, first.len) == 0);
    buf_free(&first);
    buf_free(&second);
    arena_free_all(&f.arena);
}

void test_opt_inline_growth_budget_persists_across_fixpoint_calls(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;
    Buf src;
    u32 i;

    inline_fix_init(&f);
    buf_init(&src);
    buf_printf(&src, "sym @sink\n"
                     "func void @leaf() internal {\n"
                     "entry():\n");
    for (i = 0; i < 50; i++)
        buf_printf(&src, "    call void @sink()\n");
    buf_printf(&src, "    ret\n"
                     "}\n"
                     "func void @caller() {\n"
                     "entry():\n"
                     "    call void @leaf()\n"
                     "    call void @leaf()\n"
                     "    call void @leaf()\n"
                     "    ret\n"
                     "}\n");
    buf_push_u8(&src, 0);
    m = inline_parse(&f, (char *)src.data);
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O3);
    T_ASSERT(t, m && opt_inline(m, &cfg));
    T_ASSERT_EQ_INT(t, m ? func_internal_calls_to(&m->funcs[1], 0) : 0, 1);
    /* A second scalar-fixpoint visit must not replenish the first visit's
     * instruction allowance and admit the remaining call. */
    T_ASSERT(t, m && !opt_inline(m, &cfg));
    T_ASSERT_EQ_INT(t, m ? func_internal_calls_to(&m->funcs[1], 0) : 0, 1);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    buf_free(&src);
    arena_free_all(&f.arena);
}

void test_opt_inline_growth_budget_resets_for_new_pipeline(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;

    inline_fix_init(&f);
    m = inline_parse(&f, "func i32 @leaf(i32 %x) internal {\n"
                         "entry():\n"
                         "    ret i32 %x\n"
                         "}\n"
                         "func i32 @caller(i32 %x) {\n"
                         "entry():\n"
                         "    %r = call i32 @leaf(i32 %x)\n"
                         "    ret i32 %r\n"
                         "}\n");
    T_ASSERT(t, m != NULL && ir_verify(f.dc, m));
    if (m) {
        m->funcs[1].opt_inline_growth_initialized = true;
        m->funcs[1].opt_inline_growth_left = 0;
    }
    opt_config_init(&cfg, OPT_O2);
    T_ASSERT(t, m && opt_run_pipeline(m, &cfg));
    T_ASSERT_EQ_INT(t, m ? m->nfuncs : 0, 1);
    T_ASSERT_EQ_STR(t, m && m->nfuncs ? m->funcs[0].name : NULL, "caller");
    T_ASSERT_EQ_INT(
        t, m && m->nfuncs ? func_op_count(&m->funcs[0], IR_CALL) : 1, 0);
    T_ASSERT(t, m && ir_verify(f.dc, m));
    arena_free_all(&f.arena);
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

void test_opt_force_inline_o0_strip_and_retention(TestCtx *t)
{
    static const char inline_only_src[] =
        "func i32 @forced(i32 %x) always_inline inline_only {\n"
        "entry():\n"
        "    %a = iadd i32 %x, 1\n"
        "    ret i32 %a\n"
        "}\n"
        "func i32 @caller(i32 %x) {\n"
        "entry():\n"
        "    %r = call i32 @forced(i32 %x)\n"
        "    ret i32 %r\n"
        "}\n";
    static const char retained_src[] =
        "func i32 @forced(i32 %x) always_inline {\n"
        "entry():\n"
        "    ret i32 %x\n"
        "}\n"
        "func i32 @caller(i32 %x) {\n"
        "entry():\n"
        "    %r = call i32 @forced(i32 %x)\n"
        "    ret i32 %r\n"
        "}\n";
    u32 debug;

    for (debug = 0; debug < 2; debug++) {
        InlineFix f;
        IrModule *m;
        IrModule *round;
        OptConfig cfg;
        Buf text;
        char *printed;

        inline_fix_init(&f);
        m = inline_parse(&f, inline_only_src);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        printed = inline_print(m, &text);
        T_ASSERT(t, strstr(printed, " always_inline inline_only {") != NULL);
        round = ir_parse_module(&f.arena, f.dc, printed,
                                "<force-inline-roundtrip>");
        T_ASSERT(t, round && ir_module_struct_eq(m, round));
        opt_config_init(&cfg, OPT_O0);
        cfg.debug_info = debug != 0;
        cfg.verify_after_each = true;
        T_ASSERT(t, m && opt_run_pipeline(m, &cfg));
        T_ASSERT_EQ_INT(t, m ? m->nfuncs : 0, 1);
        T_ASSERT_EQ_STR(t, m && m->nfuncs ? m->funcs[0].name : NULL, "caller");
        T_ASSERT_EQ_INT(
            t, m && m->nfuncs ? func_op_count(&m->funcs[0], IR_CALL) : 1, 0);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        buf_free(&text);
        arena_free_all(&f.arena);
    }

    {
        InlineFix f;
        IrModule *m;
        OptConfig cfg;

        inline_fix_init(&f);
        m = inline_parse(&f, retained_src);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        opt_config_init(&cfg, OPT_O0);
        cfg.verify_after_each = true;
        T_ASSERT(t, m && opt_run_pipeline(m, &cfg));
        T_ASSERT_EQ_INT(t, m ? m->nfuncs : 0, 2);
        T_ASSERT_EQ_INT(
            t, m && m->nfuncs > 1 ? func_op_count(&m->funcs[1], IR_CALL) : 1,
            0);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        arena_free_all(&f.arena);
    }
}

void test_opt_force_inline_recursive_call_is_diagnostic(TestCtx *t)
{
    InlineFix f;
    IrModule *m;
    OptConfig cfg;

    inline_fix_init(&f);
    m = inline_parse(&f,
                     "func i32 @forced(i32 %x) always_inline inline_only {\n"
                     "entry():\n"
                     "    %r = call i32 @forced(i32 %x)\n"
                     "    ret i32 %r\n"
                     "}\n");
    T_ASSERT(t, m && ir_verify(f.dc, m));
    opt_config_init(&cfg, OPT_O0);
    T_ASSERT(t, m && !opt_run_pipeline(m, &cfg));
    T_ASSERT(t, diag_had_error(f.dc));
    T_ASSERT_EQ_INT(t, m ? m->nfuncs : 0, 1);
    arena_free_all(&f.arena);
}

void test_opt_force_inline_uses_narrow_legality_rules(TestCtx *t)
{
    static const char *const accepted[] = {
        /* The full callgraph is recursive, but the forced-target graph is
         * not: after @forced is spliced, @outer's self-call is ordinary. */
        "func void @forced() always_inline inline_only {\n"
        "entry():\n    call void @outer()\n    ret\n}\n"
        "func void @outer() {\n"
        "entry():\n    call void @forced()\n    ret\n}\n",
        /* An old-style body is safe when this particular call matches its
         * concrete lowered parameter signature exactly. */
        "func void @forced() unproto always_inline inline_only {\n"
        "entry():\n    ret\n}\n"
        "func void @caller() {\n"
        "entry():\n    call void @forced()\n    ret\n}\n",
        /* Constant allocas are backend-assigned frame objects, so cloning one
         * into a cyclic CFG does not dynamically grow the stack. */
        "func void @forced() always_inline inline_only {\n"
        "entry():\n    %p = alloca 8, align 8\n    ret\n}\n"
        "func void @caller(i32 %c) {\n"
        "entry():\n    br loop()\n"
        "loop():\n    call void @forced()\n"
        "    condbr %c, loop(), exit()\n"
        "exit():\n    ret\n}\n",
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(accepted); i++) {
        InlineFix f;
        IrModule *m;
        OptConfig cfg;

        inline_fix_init(&f);
        m = inline_parse(&f, accepted[i]);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        opt_config_init(&cfg, OPT_O0);
        cfg.verify_after_each = true;
        T_ASSERT(t, m && opt_run_pipeline(m, &cfg));
        T_ASSERT(t, !diag_had_error(f.dc));
        T_ASSERT_EQ_INT(t, m ? m->nfuncs : 0, 1);
        T_ASSERT(t, m && ir_verify(f.dc, m));
        arena_free_all(&f.arena);
    }

    {
        InlineFix f;
        IrModule *m;
        OptConfig cfg;

        inline_fix_init(&f);
        m = inline_parse(
            &f, "func void @forced(i64 %n) always_inline inline_only {\n"
                "entry():\n    %p = alloca %n, align 8\n    ret\n}\n"
                "func void @caller(i32 %c, i64 %n) {\n"
                "entry():\n    br loop()\n"
                "loop():\n    call void @forced(i64 %n)\n"
                "    condbr %c, loop(), exit()\n"
                "exit():\n    ret\n}\n");
        T_ASSERT(t, m && ir_verify(f.dc, m));
        opt_config_init(&cfg, OPT_O0);
        T_ASSERT(t, m && !opt_run_pipeline(m, &cfg));
        T_ASSERT(t, diag_had_error(f.dc));
        T_ASSERT_EQ_INT(t, m ? m->nfuncs : 0, 2);
        arena_free_all(&f.arena);
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
