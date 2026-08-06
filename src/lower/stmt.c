#include "lower/lower.h"

#include <stdio.h>
#include <string.h>

/* Statement lowering: all of C11 6.8. Control flow builds on three
 * mechanisms and nothing else:
 *
 *  - the LoopCtx stack (break/continue targets; a switch pushes a
 *    break-only entry, so `continue` inside a switch inside a loop still
 *    reaches the loop — 6.8.6.2 semantics for free);
 *  - function-scope label blocks, all created before the body lowers
 *    (goto forward references are the norm);
 *  - per-switch case blocks, ALSO all created before the body lowers.
 *    Two-pass is mandatory, not a style choice: in Duff's device the
 *    `case 7:` label sits INSIDE a do-while whose body is lowered before
 *    the label is reached textually — a single-pass lowering has nowhere
 *    to aim the fallthrough edge. Pass one walks the statement subtree
 *    collecting case/default nodes (NOT descending into nested switches);
 *    pass two lowers normally, and each case label terminates the current
 *    block with a fallthrough `br` into the label's block. */

static bool stmt_is_terminated(Lower *lo)
{
    return lo->terminated;
}

/* Conditions derived from target/layout facts or macro spellings are common
 * portability idioms.  Preserve that source fact on the branch before
 * constant folding erases the expression tree; -Wunreachable-code uses it
 * to avoid configuration-dependent false positives. */
static bool expr_is_configuration_dependent(const AstNode *e)
{
    u32 i;

    if (!e)
        return false;
    if ((e->span.origin & SPAN_ORIGIN_ANY_MACRO) != 0 ||
        e->kind == AST_EXPR_SIZEOF || e->kind == AST_EXPR_ALIGNOF)
        return true;
    if (expr_is_configuration_dependent(e->lhs) ||
        expr_is_configuration_dependent(e->mid) ||
        expr_is_configuration_dependent(e->rhs))
        return true;
    for (i = 0; i < e->nargs; i++)
        if (expr_is_configuration_dependent(e->args[i]))
            return true;
    for (i = 0; i < e->nitems; i++)
        if (expr_is_configuration_dependent(e->items[i]))
            return true;
    return false;
}

static void mark_config_branch(Lower *lo, const AstNode *condition)
{
    if (expr_is_configuration_dependent(condition))
        ir_branch_mark_flow_provenance(&lo->b);
}

/* Terminate-then-continue: statements after a terminator open a fresh
 * block. If nothing ever branches to it, ir_func_remove_unreachable
 * deletes it after the function completes — the verifier's orphan check
 * demands deletion, not abandonment. */
static void ensure_open_block(Lower *lo, const char *why)
{
    if (stmt_is_terminated(lo)) {
        BlockId block = lower_new_block(lo, why);
        Span at = ir_builder_span(&lo->b);
        u32 region = lo->dead_region;

        if (!region)
            region = ++lo->next_dead_region;

        /* Preserve the exact statement span before orphan cleanup. Empty
         * statements otherwise leave no instruction to carry it. */
        ir_func_record_removed_region(lo->fn, block, at, region, 0);
        lower_at(lo, block);
        lo->dead_region = region;
    }
}

/* --- VLA scope machinery (Sprint 20) ---------------------------------------
 */

static bool type_is_vla_chain(const Type *t)
{
    for (; t; t = t->base)
        if (t->kind == TY_ARRAY && t->is_vla)
            return true;
        else if (t->kind != TY_ARRAY)
            break;
    return false;
}

/* C17 6.7.6.2p4: the size expression of a VLA type is evaluated when the
 * DECLARATION is reached, whether or not that declaration creates an object
 * of the type. `char (*p)[width]` declares a POINTER, so type_is_vla_chain
 * never sees it -- yet `p[i]` still has to scale by `width`.
 *
 * Computing that lazily at the first use put the definition wherever the
 * first subscript happened to be, which in musl's lsearch.c is inside a loop
 * body that does not dominate the later use: IR verify check 1. Priming the
 * cache at the declaration gives every use one dominating definition and
 * evaluates the expression exactly once, in the place the standard names.
 *
 * Walking base through arrays AND pointers is the whole point; lower_type_size
 * already recurses through an array's element type, so caching the outermost
 * VLA caches everything under it. */
static void lower_vla_sizes_in_decl(Lower *lo, Type *t)
{
    for (; t; t = t->base) {
        if (t->kind == TY_ARRAY && t->is_vla)
            (void)lower_type_size(lo, t);
        else if (t->kind != TY_ARRAY && t->kind != TY_PTR)
            break;
    }
}

/* Restores the OUTERMOST live token among the scopes from the innermost
 * up to (but excluding) `stop` — one restore subsumes the inner ones. */
static void vla_restore_until(Lower *lo, VlaScope *stop)
{
    VlaScope *sc;
    ValueId outer = VALUE_INVALID;

    for (sc = lo->vla_scopes; sc && sc != stop; sc = sc->prev)
        if (sc->token.v)
            outer = sc->token;
    if (outer.v)
        ir_build_stackrestore(&lo->b, ir_op_value(lo->fn, outer));
}

/* goto: restore down to the label's innermost VLA compound (sema already
 * proved the label's chain is a subset of ours). */
static void vla_restore_for_goto(Lower *lo, const char *label)
{
    void *hit = strmap_get(&lo->label_vla, label, strlen(label));
    const AstNode *target = hit; /* innermost VLA compound at the label */
    VlaScope *sc;
    ValueId outer = VALUE_INVALID;

    for (sc = lo->vla_scopes; sc; sc = sc->prev) {
        if (sc->compound == target)
            break;
        if (sc->token.v)
            outer = sc->token;
    }
    if (outer.v)
        ir_build_stackrestore(&lo->b, ir_op_value(lo->fn, outer));
}

/* --- local declarations --------------------------------------------------- */

static void lower_local_static(Lower *lo, Symbol *sym, AstNode *init)
{
    /* A block-scope static is a file-lifetime object with a mangled
     * internal name (deterministic: name.N in encounter order). */
    char buf[192];
    IrGlobal *g;
    TypeLayout l;
    u32 idx;

    if (!sym->type || !layout_is_complete_for_size(sym->type))
        return;
    snprintf(buf, sizeof(buf), "%s.%u", sym->name, lo->nlocal_static++);
    l = layout_of(lo->sema, sym->type);
    g = ir_global_new(lo->m, arena_strdup(lo->arena, buf));
    g->align = (u32)(l.align ? l.align : 1);
    g->linkage = IRLINK_INTERNAL;
    g->size = l.size;
    if (init) {
        InitImage img;

        if (constexpr_eval_initializer(lo->sema, sym->type, init, &img)) {
            u32 i;

            g->size = img.size;
            g->init = img.bytes;
            if (img.nrelocs) {
                g->relocs =
                    arena_alloc(lo->arena, img.nrelocs * sizeof(IrReloc),
                                _Alignof(IrReloc));
                g->nrelocs = img.nrelocs;
                for (i = 0; i < img.nrelocs; i++) {
                    g->relocs[i].offset = img.relocs[i].offset;
                    g->relocs[i].addend = img.relocs[i].addend;
                    g->relocs[i].symbol =
                        img.relocs[i].sym
                            ? lower_global_sym(lo, img.relocs[i].sym)
                            : lower_anon_sym(lo, img.relocs[i].anon);
                }
            }
        }
    }
    idx = ir_sym(lo->m, g->name);
    /* Route every later USE of this symbol to the mangled global. */
    lower_bind_static(lo, sym, idx);
}

static void lower_one_decl(Lower *lo, AstNode *d)
{
    Symbol *sym = d->sym;
    TypeLayout l;
    ValueId slot;

    if (!d->name || (d->storage & AST_SC_TYPEDEF))
        return;
    if (!sym || sym->kind != SYM_VAR)
        return; /* typedef/tag/enum-only declarations */
    lower_vla_sizes_in_decl(lo, sym->type);
    if (sym->type && type_is_vla_chain(sym->type)) {
        /* VLA: the size expressions evaluate exactly ONCE, here, in
         * declaration order (lower_type_size caches per Type node — a
         * later sizeof READS the cache). The scope's stacksave is lazy:
         * emitted at its first VLA. */
        IrOperand bytes = lower_type_size(lo, sym->type);
        Type *elem = sym->type;
        TypeLayout el;

        while (elem->kind == TY_ARRAY)
            elem = elem->base;
        el = layout_of(lo->sema, elem);
        if (lo->vla_scopes && !lo->vla_scopes->token.v)
            lo->vla_scopes->token = ir_build_stacksave(&lo->b);
        slot =
            ir_build_alloca_typed(&lo->b, bytes, (u32)(el.align ? el.align : 1),
                                  lower_efftype(lo, sym->type));
        lower_bind_local(lo, sym, slot);
        if (lo->auto_var_init != LOWER_AUTO_VAR_INIT_NONE &&
            !(d->storage & AST_SC_THREAD_LOCAL)) {
            i64 byte =
                lo->auto_var_init == LOWER_AUTO_VAR_INIT_PATTERN ? 0xfe : 0;
            IrOperand args[3];

            /* The x86 backend's inline IR_MEMSET form is constant-size by
             * contract.  A VLA is necessarily runtime-sized, so lower its
             * mitigation through the ordinary hosted memset ABI instead of
             * sending an unsupported dynamic intrinsic to instruction
             * selection. */
            args[0] = ir_op_value(lo->fn, slot);
            args[1] = ir_op_iconst(IRT_I32, byte);
            args[2] = bytes;
            (void)ir_build_call(&lo->b, IRT_PTR, FUNCREF_EXTERNAL,
                                ir_sym(lo->m, "memset"), args, 3);
        }
        return;
    }
    if (d->storage & AST_SC_STATIC) {
        lower_local_static(lo, sym, d->init);
        return;
    }
    if (d->storage & AST_SC_EXTERN)
        return; /* block-scope extern: references resolve via the symbol */
    if (!sym->type || !layout_is_complete_for_size(sym->type))
        return;

    slot = lower_local_slot(lo, sym);
    if (!slot.v) {
        l = layout_of(lo->sema, sym->type);
        slot = ir_build_alloca_typed(
            &lo->b, lower_i64((i64)(l.size ? l.size : 1)),
            (u32)(l.align ? l.align : 1), lower_efftype(lo, sym->type));
        lower_bind_local(lo, sym, slot);
    }
    if (d->init) {
        Symbol *saved_init = lo->initializing_sym;
        AstNode *self = d->init;

        while (self && self->kind == AST_INIT_LIST && self->nitems == 1)
            self = self->items[0];
        while (self && (self->kind == AST_EXPR_PAREN ||
                        (self->kind == AST_EXPR_CAST && self->implicit)))
            self = self->lhs;
        lo->initializing_sym =
            self && self->kind == AST_EXPR_IDENT && self->sym == sym ? sym
                                                                     : NULL;
        lower_local_init(lo, ir_op_value(lo->fn, slot), sym->type, d->init);
        lo->initializing_sym = saved_init;
    } else if (lo->auto_var_init != LOWER_AUTO_VAR_INIT_NONE &&
               !(d->storage & AST_SC_THREAD_LOCAL)) {
        i64 byte = 0;

        if (lo->auto_var_init == LOWER_AUTO_VAR_INIT_PATTERN) {
            /* 0xFE makes accidental pointer values non-canonical on x86-64,
             * so a dereference tends to trap instead of behaving like NULL. */
            byte = 0xfe;
        }
        l = layout_of(lo->sema, sym->type);
        ir_build_memset(&lo->b, ir_op_value(lo->fn, slot),
                        ir_op_iconst(IRT_I32, byte), lower_i64((i64)l.size),
                        (u32)(l.align ? l.align : 1), 0);
    }
}

static void prebind_one_decl(Lower *lo, AstNode *d)
{
    Symbol *sym;
    TypeLayout l;
    ValueId slot;

    if (!d || !d->name || (d->storage & AST_SC_TYPEDEF))
        return;
    sym = d->sym;
    if (!sym || sym->kind != SYM_VAR || (d->storage & AST_SC_STATIC) ||
        (d->storage & AST_SC_EXTERN) || !sym->type ||
        type_is_vla_chain(sym->type) ||
        !layout_is_complete_for_size(sym->type) || lower_local_slot(lo, sym).v)
        return;
    l = layout_of(lo->sema, sym->type);
    slot = ir_build_alloca_typed(&lo->b, lower_i64((i64)(l.size ? l.size : 1)),
                                 (u32)(l.align ? l.align : 1),
                                 lower_efftype(lo, sym->type));
    lower_bind_local(lo, sym, slot);
}

static void prebind_decl_group(Lower *lo, AstNode *d)
{
    u32 i;

    if (!d)
        return;
    prebind_one_decl(lo, d);
    for (i = 0; i < d->nitems; i++)
        prebind_one_decl(lo, d->items[i]);
}

void lower_prebind_locals(Lower *lo, AstNode *s)
{
    u32 i;

    if (!s)
        return;
    switch (s->kind) {
    case AST_STMT_DECL:
        prebind_decl_group(lo, s->lhs);
        return;
    case AST_STMT_COMPOUND:
        for (i = 0; i < s->nitems; i++)
            lower_prebind_locals(lo, s->items[i]);
        return;
    case AST_STMT_IF:
        lower_prebind_locals(lo, s->body);
        lower_prebind_locals(lo, s->rhs);
        return;
    case AST_STMT_SWITCH:
    case AST_STMT_WHILE:
    case AST_STMT_DO:
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT:
    case AST_STMT_LABEL:
        lower_prebind_locals(lo, s->body);
        return;
    case AST_STMT_FOR:
        if (s->lhs && s->lhs->kind == AST_DECL)
            prebind_decl_group(lo, s->lhs);
        lower_prebind_locals(lo, s->body);
        return;
    default:
        return;
    }
}

static void lower_local_decl(Lower *lo, AstNode *d)
{
    u32 i;

    if (!d)
        return;
    /* Sibling declarators (`int i, j = 1;`) ride the head node's items,
     * in source order — which is also their initializer side-effect
     * order under the §1 law. */
    lower_one_decl(lo, d);
    for (i = 0; i < d->nitems; i++)
        if (d->items[i])
            lower_one_decl(lo, d->items[i]);
}

/* --- switch: the case-collecting pre-pass ----------------------------------
 */

static void collect_cases(Lower *lo, SwitchCtx *ctx, AstNode *s)
{
    u32 i;

    if (!s)
        return;
    switch (s->kind) {
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT: {
        SwitchCase *c =
            arena_alloc(lo->arena, sizeof(SwitchCase), _Alignof(SwitchCase));

        memset(c, 0, sizeof(*c));
        c->stmt = s;
        c->is_default = s->kind == AST_STMT_DEFAULT;
        if (!c->is_default) {
            ConstValue cv = constexpr_eval(lo->sema, s->lhs, CE_FOLD);

            c->value = cv.kind == CV_INT ? (i64)cv.i : 0;
        }
        c->block =
            lower_new_block(lo, c->is_default ? "sw.default" : "sw.case");
        /* Append preserving SOURCE order — the IR table sorts later,
         * but block creation order stays document order. */
        {
            SwitchCase **tail = &ctx->cases;

            while (*tail)
                tail = &(*tail)->next;
            *tail = c;
        }
        collect_cases(lo, ctx, s->body);
        return;
    }
    case AST_STMT_SWITCH:
        return; /* nested switch owns its own cases */
    case AST_STMT_COMPOUND:
        for (i = 0; i < s->nitems; i++)
            collect_cases(lo, ctx, s->items[i]);
        return;
    case AST_STMT_IF:
        collect_cases(lo, ctx, s->body);
        collect_cases(lo, ctx, s->rhs);
        return;
    case AST_STMT_WHILE:
    case AST_STMT_DO:
    case AST_STMT_FOR:
    case AST_STMT_LABEL:
        collect_cases(lo, ctx, s->body);
        return;
    default:
        return;
    }
}

static void lower_switch(Lower *lo, AstNode *s)
{
    SwitchCtx ctx;
    LoopCtx brk;
    IrOperand scrut = lower_rvalue(lo, s->lhs);
    BlockId join;
    SwitchCase *c;
    u32 ncases = 0;
    BlockId defblk = BLOCK_INVALID;

    memset(&ctx, 0, sizeof(ctx));
    collect_cases(lo, &ctx, s->body);
    join = lower_new_block(lo, "sw.join");

    for (c = ctx.cases; c; c = c->next) {
        if (c->is_default)
            defblk = c->block;
        else
            ncases++;
    }
    if (defblk.v == 0)
        defblk = join; /* no default: fall past the switch */

    /* The IR terminator carries a SORTED case table (backends choose
     * jump-table vs tree from it; the IR just keeps it canonical). */
    {
        i64 *vals = arena_alloc(lo->arena, (ncases ? ncases : 1) * sizeof(i64),
                                _Alignof(i64));
        BlockId *blks =
            arena_alloc(lo->arena, (ncases ? ncases : 1) * sizeof(BlockId),
                        _Alignof(BlockId));
        u32 n = 0;
        u32 i, j;

        for (c = ctx.cases; c; c = c->next)
            if (!c->is_default) {
                vals[n] = c->value;
                blks[n] = c->block;
                n++;
            }
        /* insertion sort by value — n is small and this is deterministic */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && vals[j - 1] > vals[j]; j--) {
                i64 tv = vals[j];
                BlockId tb = blks[j];

                vals[j] = vals[j - 1];
                blks[j] = blks[j - 1];
                vals[j - 1] = tv;
                blks[j - 1] = tb;
            }
        ir_build_switch(&lo->b, scrut, defblk, vals, blks, n);
        mark_config_branch(lo, s->lhs);
        lo->terminated = true;
    }

    /* Body lowering: push contexts, then lower the body statement with
     * the current block CLOSED — the first thing reachable inside is a
     * case label, which opens its block. */
    brk.break_target = join;
    brk.continue_target = BLOCK_INVALID; /* continue skips switch entries */
    brk.vla_mark = lo->vla_scopes;
    brk.prev = lo->loops;
    lo->loops = &brk;
    ctx.prev = lo->switches;
    lo->switches = &ctx;

    lower_stmt(lo, s->body);
    if (!lo->terminated)
        ir_build_br(&lo->b, join, NULL, 0); /* last case falls out */

    lo->switches = ctx.prev;
    lo->loops = brk.prev;
    lower_at(lo, join);
}

/* The pre-pass block for a case/default node of the INNERMOST switch. */
static BlockId case_block_of(Lower *lo, const AstNode *s)
{
    SwitchCase *c;

    if (!lo->switches)
        return BLOCK_INVALID;
    for (c = lo->switches->cases; c; c = c->next)
        if (c->stmt == s)
            return c->block;
    return BLOCK_INVALID;
}

/* --- statements ------------------------------------------------------------
 */

static void lower_branch_to(Lower *lo, BlockId b)
{
    if (!lo->terminated) {
        ir_build_br(&lo->b, b, NULL, 0);
        lo->terminated = true;
    }
}

static void lower_stmt_impl(Lower *lo, AstNode *s)
{
    u32 i;

    if (!s || lo->failed)
        return;
    switch (s->kind) {
    case AST_STMT_COMPOUND: {
        VlaScope scope;

        scope.token = VALUE_INVALID;
        scope.compound = s;
        scope.prev = lo->vla_scopes;
        lo->vla_scopes = &scope;
        for (i = 0; i < s->nitems; i++)
            lower_stmt(lo, s->items[i]);
        /* Normal fall-out: rewind this scope's VLAs (if any). */
        if (scope.token.v && !lo->terminated)
            ir_build_stackrestore(&lo->b, ir_op_value(lo->fn, scope.token));
        lo->vla_scopes = scope.prev;
        return;
    }
    case AST_STMT_DECL:
        ensure_open_block(lo, "dead");
        lower_local_decl(lo, s->lhs);
        return;
    case AST_STMT_EXPR:
        ensure_open_block(lo, "dead");
        (void)lower_rvalue(lo, s->lhs);
        return;
    case AST_STMT_NULL:
        ensure_open_block(lo, "dead");
        return;
    case AST_STMT_IF: {
        BlockId tb, eb, join;
        IrOperand c;

        ensure_open_block(lo, "dead");
        c = lower_cond(lo, s->lhs);
        tb = lower_new_block(lo, "if.then");
        join = lower_new_block(lo, "if.join");
        eb = s->rhs ? lower_new_block(lo, "if.else") : join;
        ir_build_condbr(&lo->b, c, tb, NULL, 0, eb, NULL, 0);
        mark_config_branch(lo, s->lhs);
        lower_at(lo, tb);
        lower_stmt(lo, s->body);
        lower_branch_to(lo, join);
        if (s->rhs) {
            lower_at(lo, eb);
            lower_stmt(lo, s->rhs);
            lower_branch_to(lo, join);
        }
        lower_at(lo, join);
        return;
    }
    case AST_STMT_WHILE: {
        BlockId header, body, exit_;
        LoopCtx lc;
        IrOperand c;

        ensure_open_block(lo, "dead");
        header = lower_new_block(lo, "while.head");
        body = lower_new_block(lo, "while.body");
        exit_ = lower_new_block(lo, "while.exit");
        lower_branch_to(lo, header);
        lower_at(lo, header);
        c = lower_cond(lo, s->lhs);
        ir_build_condbr(&lo->b, c, body, NULL, 0, exit_, NULL, 0);
        mark_config_branch(lo, s->lhs);
        lc.break_target = exit_;
        lc.continue_target = header;
        lc.vla_mark = lo->vla_scopes;
        lc.prev = lo->loops;
        lo->loops = &lc;
        lower_at(lo, body);
        lower_stmt(lo, s->body);
        lower_branch_to(lo, header);
        lo->loops = lc.prev;
        lower_at(lo, exit_);
        return;
    }
    case AST_STMT_DO: {
        BlockId body, cond, exit_;
        LoopCtx lc;
        IrOperand c;

        ensure_open_block(lo, "dead");
        body = lower_new_block(lo, "do.body");
        cond = lower_new_block(lo, "do.cond");
        exit_ = lower_new_block(lo, "do.exit");
        lower_branch_to(lo, body);
        lc.break_target = exit_;
        lc.continue_target = cond; /* continue re-tests, per 6.8.6.2 */
        lc.vla_mark = lo->vla_scopes;
        lc.prev = lo->loops;
        lo->loops = &lc;
        lower_at(lo, body);
        lower_stmt(lo, s->body);
        lower_branch_to(lo, cond);
        lo->loops = lc.prev;
        lower_at(lo, cond);
        c = lower_cond(lo, s->lhs);
        ir_build_condbr(&lo->b, c, body, NULL, 0, exit_, NULL, 0);
        mark_config_branch(lo, s->lhs);
        lower_at(lo, exit_);
        return;
    }
    case AST_STMT_FOR: {
        BlockId header, body, step, exit_;
        LoopCtx lc;

        ensure_open_block(lo, "dead");
        /* The C99 for-init clause is either an expression statement or a
         * BARE declaration node — the parser stores the declaration
         * directly, not wrapped in AST_STMT_DECL (the same trap sema
         * documents at its own for-loop). Dispatching on kind here is
         * what keeps `for (int i = 0; ...)` a LOCAL. */
        if (s->lhs) {
            if (s->lhs->kind == AST_DECL)
                lower_local_decl(lo, s->lhs);
            else
                lower_stmt(lo, s->lhs);
        }
        header = lower_new_block(lo, "for.head");
        body = lower_new_block(lo, "for.body");
        step = lower_new_block(lo, "for.step");
        exit_ = lower_new_block(lo, "for.exit");
        lower_branch_to(lo, header);
        lower_at(lo, header);
        if (s->mid) {
            IrOperand c = lower_cond(lo, s->mid);

            ir_build_condbr(&lo->b, c, body, NULL, 0, exit_, NULL, 0);
            mark_config_branch(lo, s->mid);
        } else {
            ir_build_br(&lo->b, body, NULL, 0); /* for(;;) — no compare */
        }
        lc.break_target = exit_;
        lc.continue_target = step; /* the STEP block, not the header */
        lc.vla_mark = lo->vla_scopes;
        lc.prev = lo->loops;
        lo->loops = &lc;
        lower_at(lo, body);
        lower_stmt(lo, s->body);
        lower_branch_to(lo, step);
        lo->loops = lc.prev;
        lower_at(lo, step);
        if (s->rhs)
            (void)lower_rvalue(lo, s->rhs);
        lower_branch_to(lo, header);
        lower_at(lo, exit_);
        return;
    }
    case AST_STMT_SWITCH:
        ensure_open_block(lo, "dead");
        lower_switch(lo, s);
        return;
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT: {
        BlockId b = case_block_of(lo, s);

        if (b.v == 0) {
            lower_stmt(lo, s->body);
            return;
        }
        /* Fallthrough from the previous case is an explicit edge. */
        lower_branch_to(lo, b);
        lower_at(lo, b);
        lower_stmt(lo, s->body);
        return;
    }
    case AST_STMT_LABEL: {
        u32 *hit = lower_u32map_get(&lo->labels, s->name, strlen(s->name));

        if (hit) {
            BlockId b = {*hit};

            lower_branch_to(lo, b); /* fallthrough into the label */
            lower_at(lo, b);
        }
        lower_stmt(lo, s->body);
        return;
    }
    case AST_STMT_GOTO: {
        u32 *hit = lower_u32map_get(&lo->labels, s->name, strlen(s->name));

        ensure_open_block(lo, "dead");
        if (hit) {
            BlockId b = {*hit};

            vla_restore_for_goto(lo, s->name);
            ir_build_br(&lo->b, b, NULL, 0);
            lo->terminated = true;
        }
        return;
    }
    case AST_STMT_BREAK: {
        LoopCtx *lc = lo->loops;

        ensure_open_block(lo, "dead");
        if (lc) {
            vla_restore_until(lo, lc->vla_mark);
            ir_build_br(&lo->b, lc->break_target, NULL, 0);
            if (lc->continue_target.v == 0)
                ir_branch_mark_flow_provenance(&lo->b);
            lo->terminated = true;
        }
        return;
    }
    case AST_STMT_CONTINUE: {
        LoopCtx *lc = lo->loops;

        ensure_open_block(lo, "dead");
        while (lc && lc->continue_target.v == 0)
            lc = lc->prev; /* skip switch entries */
        if (lc) {
            vla_restore_until(lo, lc->vla_mark);
            ir_build_br(&lo->b, lc->continue_target, NULL, 0);
            lo->terminated = true;
        }
        return;
    }
    case AST_STMT_RETURN:
        /* No stackrestore on return: the epilogue's frame teardown
         * subsumes every live VLA token (and longjmp likewise unwinds
         * frames wholesale — tokens die with them). */
        ensure_open_block(lo, "dead");
        if (lo->cur_return_type && lo->cur_return_type->kind == TY_VOID) {
            /* Sema already warned about `return expr` here. Preserve its
             * side effects while keeping warning-only IR verifier-valid. */
            if (s->lhs)
                (void)lower_rvalue(lo, s->lhs);
            ir_build_ret(&lo->b, NULL);
        } else if (!s->lhs) {
            /* A missing value is a warning. Scalar and small-aggregate IR
             * functions nevertheless require a return operand. */
            if (lo->fn->ret == IRT_VOID) {
                ir_build_ret(&lo->b, NULL);
            } else {
                IrOperand undef = ir_op_undef((IrType)lo->fn->ret);

                ir_build_ret(&lo->b, &undef);
            }
        } else if (lo->sret.v) {
            /* SRET/PAIR aggregate return: memcpy into the hidden result
             * pointer, then ret void (the register story is the
             * IrAbiRet annotation's — see ir.h). */
            IrOperand src = lower_rvalue(lo, s->lhs);
            TypeLayout l = layout_of(lo->sema, s->lhs->sem_type);

            lower_memcpy_aggregate(lo, ir_op_value(lo->fn, lo->sret), src,
                                   s->lhs->sem_type, (u32)l.align, 0);
            ir_build_ret(&lo->b, NULL);
        } else if (s->lhs && lo->cur_abi_ret &&
                   lo->cur_abi_ret->kind == ABI_RET_SMALL) {
            /* One-eightbyte aggregate: the VALUE travels as a
             * bit-carrying i64/f64. Load through an 8-byte staging slot
             * when the object is shorter than the load. */
            IrOperand src = lower_rvalue(lo, s->lhs);
            AbiRet *ar = lo->cur_abi_ret;
            IrOperand from = src;
            Lvalue lv;
            IrOperand v;

            if (ar->size < 8) {
                ValueId tmp = ir_build_alloca(&lo->b, lower_i64(8), 8);

                lower_memcpy_aggregate(lo, ir_op_value(lo->fn, tmp), src,
                                       s->lhs->sem_type, ar->align, 0);
                from = ir_op_value(lo->fn, tmp);
            }
            memset(&lv, 0, sizeof(lv));
            lv.addr = from;
            lv.unit = ar->small_t;
            lv.align = 8;
            v = lower_load(lo, lv);
            ir_build_ret(&lo->b, &v);
        } else if (s->lhs) {
            IrOperand v = lower_rvalue(lo, s->lhs);

            ir_build_ret(&lo->b, &v);
        }
        lo->terminated = true;
        return;
    default:
        return;
    }
}

void lower_stmt(Lower *lo, AstNode *s)
{
    Span saved;

    if (!s || lo->failed)
        return;
    saved = ir_builder_span(&lo->b);
    ir_builder_set_span(&lo->b, s->span);
    lower_stmt_impl(lo, s);
    ir_builder_set_span(&lo->b, saved);
}
