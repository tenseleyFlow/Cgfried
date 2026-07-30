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

/* Terminate-then-continue: statements after a terminator open a fresh
 * block. If nothing ever branches to it, ir_func_remove_unreachable
 * deletes it after the function completes — the verifier's orphan check
 * demands deletion, not abandonment. */
static void ensure_open_block(Lower *lo, const char *why)
{
    if (stmt_is_terminated(lo))
        lower_at(lo, lower_new_block(lo, why));
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
                            : lower_string_lit(lo, img.relocs[i].anon);
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
    if (sym->type && sym->type->is_vla) {
        lower_unimplemented(lo, d->span,
                            "a variable-length array "
                            "declaration",
                            20);
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

    l = layout_of(lo->sema, sym->type);
    slot = ir_build_alloca(&lo->b, lower_i64((i64)(l.size ? l.size : 1)),
                           (u32)(l.align ? l.align : 1));
    lower_bind_local(lo, sym, slot);
    if (d->init)
        lower_local_init(lo, ir_op_value(lo->fn, slot), sym->type, d->init);
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

/* --- the runtime initializer walk (mirrors constexpr.c's fill()) ---------- */

static void init_store_scalar(Lower *lo, IrOperand base, i64 off, Type *t,
                              AstNode *e)
{
    IrOperand addr = base;
    IrOperand v;
    Lvalue lv;

    if (!e)
        return;
    v = lower_rvalue(lo, e);
    v = lower_scalar_convert(lo, v, e->sem_type, t);
    if (off != 0) {
        ValueId p = ir_build_ptradd(&lo->b, base, lower_i64(off));

        addr = ir_op_value(lo->fn, p);
    }
    memset(&lv, 0, sizeof(lv));
    lv.addr = addr;
    lv.unit = lower_irtype(lo, t);
    {
        TypeLayout l = layout_of(lo->sema, t);

        lv.align = (u32)(l.align ? l.align : 1);
    }
    lower_store(lo, lv, v);
}

static void init_walk(Lower *lo, IrOperand base, i64 off, Type *t,
                      AstNode *init);

static void init_walk_array(Lower *lo, IrOperand base, i64 off, Type *t,
                            AstNode *init)
{
    TypeLayout el = layout_of(lo->sema, t->base);
    u64 idx = 0;
    u32 k;

    if (init->kind == AST_EXPR_STRING) {
        /* Copy the bytes from the string's .rodata object; the zero-fill
         * already supplied the terminator and the tail. */
        u64 cap = t->has_size ? t->size : 0;
        u64 n = init->tok ? init->tok->str.nbytes : 0;
        u32 s = lower_string_lit(lo, init);

        if (n > cap)
            n = cap;
        if (n)
            ir_build_memcpy(&lo->b, base, ir_op_symbol(IRT_PTR, s, 0),
                            lower_i64((i64)n), 1, 0);
        return;
    }
    if (init->kind != AST_INIT_LIST) {
        init_walk(lo, base, off, t->base, init);
        return;
    }
    for (k = 0; k < init->nitems; k++) {
        AstNode *item = init->items[k];

        if (!item)
            continue;
        if (item->ndesignators > 0 && item->designators[0] &&
            !item->designators[0]->desig_is_field &&
            item->designators[0]->desig_index) {
            ConstValue cv = constexpr_eval(
                lo->sema, item->designators[0]->desig_index, CE_FOLD);

            if (cv.kind == CV_INT)
                idx = cv.i;
        }
        if (!t->has_size || idx < t->size)
            init_walk(lo, base, off + (i64)(idx * el.size), t->base, item);
        idx++;
    }
}

static void init_walk_record(Lower *lo, IrOperand base, i64 off, Type *t,
                             AstNode *init)
{
    Member *m;
    u32 k;

    if (!t->tag)
        return;
    layout_record(lo->sema, t);
    if (init->kind != AST_INIT_LIST) {
        /* struct x = expr: one memcpy (the §8 law). */
        IrOperand src = lower_rvalue(lo, init);
        TypeLayout l = layout_of(lo->sema, t);
        IrOperand dst = base;

        if (off) {
            ValueId p = ir_build_ptradd(&lo->b, base, lower_i64(off));

            dst = ir_op_value(lo->fn, p);
        }
        ir_build_memcpy(&lo->b, dst, src, lower_i64((i64)l.size), (u32)l.align,
                        0);
        return;
    }
    m = t->tag->members;
    while (m && m->is_bitfield && !m->name)
        m = m->next;
    for (k = 0; k < init->nitems && m; k++) {
        AstNode *item = init->items[k];

        if (!item)
            continue;
        if (item->ndesignators > 0 && item->designators[0] &&
            item->designators[0]->desig_is_field) {
            Member *it;

            for (it = t->tag->members; it; it = it->next)
                if (it->name == item->designators[0]->desig_field) {
                    m = it;
                    break;
                }
        }
        if (m->is_bitfield) {
            Lvalue lv;
            u64 cbits = m->container_size * 8;
            u64 unit_byte = (m->bit_offset / cbits) * m->container_size;
            IrOperand v = lower_rvalue(lo, item);

            v = lower_scalar_convert(lo, v, item->sem_type, m->type);
            memset(&lv, 0, sizeof(lv));
            {
                ValueId p = ir_build_ptradd(&lo->b, base,
                                            lower_i64(off + (i64)unit_byte));

                lv.addr = ir_op_value(lo->fn, p);
            }
            switch (m->container_size) {
            case 1:
                lv.unit = IRT_I8;
                break;
            case 2:
                lv.unit = IRT_I16;
                break;
            case 4:
                lv.unit = IRT_I32;
                break;
            default:
                lv.unit = IRT_I64;
                break;
            }
            lv.align = (u32)m->container_size;
            lv.is_bitfield = true;
            lv.bit_shift = (u8)(m->bit_offset - unit_byte * 8);
            lv.bit_width = (u8)m->bit_width;
            lv.is_signed = conv_is_signed(lo->sema, m->type);
            lower_store(lo, lv, v);
        } else {
            init_walk(lo, base, off + (i64)m->offset, m->type, item);
        }
        if (t->kind == TY_UNION)
            break;
        m = m->next;
        while (m && m->is_bitfield && !m->name)
            m = m->next;
    }
}

static void init_walk(Lower *lo, IrOperand base, i64 off, Type *t,
                      AstNode *init)
{
    if (!t || !init)
        return;
    switch (t->kind) {
    case TY_ARRAY:
        init_walk_array(lo, base, off, t, init);
        return;
    case TY_STRUCT:
    case TY_UNION:
        init_walk_record(lo, base, off, t, init);
        return;
    default:
        if (init->kind == AST_INIT_LIST) {
            if (init->nitems > 0)
                init_store_scalar(lo, base, off, t, init->items[0]);
            return;
        }
        init_store_scalar(lo, base, off, t, init);
        return;
    }
}

void lower_local_init(Lower *lo, IrOperand base, Type *t, AstNode *init)
{
    if (!init)
        return;
    /* Aggregates initialized by a LIST are zero-filled first, so every
     * unmentioned element and every padding byte is deterministic — the
     * same contract as the static images. */
    if (lower_is_aggregate(t) && init->kind == AST_INIT_LIST) {
        TypeLayout l = layout_of(lo->sema, t);

        ir_build_memset(&lo->b, base, ir_op_iconst(IRT_I32, 0),
                        lower_i64((i64)l.size), (u32)(l.align ? l.align : 1),
                        0);
    }
    if (lower_is_aggregate(t) && t->kind == TY_ARRAY &&
        init->kind == AST_EXPR_STRING) {
        TypeLayout l = layout_of(lo->sema, t);

        ir_build_memset(&lo->b, base, ir_op_iconst(IRT_I32, 0),
                        lower_i64((i64)l.size), (u32)(l.align ? l.align : 1),
                        0);
    }
    init_walk(lo, base, 0, t, init);
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
        lo->terminated = true;
    }

    /* Body lowering: push contexts, then lower the body statement with
     * the current block CLOSED — the first thing reachable inside is a
     * case label, which opens its block. */
    brk.break_target = join;
    brk.continue_target = BLOCK_INVALID; /* continue skips switch entries */
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

void lower_stmt(Lower *lo, AstNode *s)
{
    u32 i;

    if (!s || lo->failed)
        return;
    switch (s->kind) {
    case AST_STMT_COMPOUND:
        for (i = 0; i < s->nitems; i++)
            lower_stmt(lo, s->items[i]);
        return;
    case AST_STMT_DECL:
        ensure_open_block(lo, "dead");
        lower_local_decl(lo, s->lhs);
        return;
    case AST_STMT_EXPR:
        ensure_open_block(lo, "dead");
        (void)lower_rvalue(lo, s->lhs);
        return;
    case AST_STMT_NULL:
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
        lc.break_target = exit_;
        lc.continue_target = header;
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
        lc.prev = lo->loops;
        lo->loops = &lc;
        lower_at(lo, body);
        lower_stmt(lo, s->body);
        lower_branch_to(lo, cond);
        lo->loops = lc.prev;
        lower_at(lo, cond);
        c = lower_cond(lo, s->lhs);
        ir_build_condbr(&lo->b, c, body, NULL, 0, exit_, NULL, 0);
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
        } else {
            ir_build_br(&lo->b, body, NULL, 0); /* for(;;) — no compare */
        }
        lc.break_target = exit_;
        lc.continue_target = step; /* the STEP block, not the header */
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
        void *hit = strmap_get(&lo->labels, s->name, strlen(s->name));

        if (hit) {
            BlockId b = {(u32)(uintptr_t)hit};

            lower_branch_to(lo, b); /* fallthrough into the label */
            lower_at(lo, b);
        }
        lower_stmt(lo, s->body);
        return;
    }
    case AST_STMT_GOTO: {
        void *hit = strmap_get(&lo->labels, s->name, strlen(s->name));

        ensure_open_block(lo, "dead");
        if (hit) {
            BlockId b = {(u32)(uintptr_t)hit};

            ir_build_br(&lo->b, b, NULL, 0);
            lo->terminated = true;
        }
        return;
    }
    case AST_STMT_BREAK: {
        LoopCtx *lc = lo->loops;

        ensure_open_block(lo, "dead");
        if (lc) {
            ir_build_br(&lo->b, lc->break_target, NULL, 0);
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
            ir_build_br(&lo->b, lc->continue_target, NULL, 0);
            lo->terminated = true;
        }
        return;
    }
    case AST_STMT_RETURN:
        ensure_open_block(lo, "dead");
        if (s->lhs && lo->sret.v) {
            /* Aggregate return: memcpy into the hidden result pointer,
             * then ret void. */
            IrOperand src = lower_rvalue(lo, s->lhs);
            TypeLayout l = layout_of(lo->sema, s->lhs->sem_type);

            ir_build_memcpy(&lo->b, ir_op_value(lo->fn, lo->sret), src,
                            lower_i64((i64)l.size), (u32)l.align, 0);
            ir_build_ret(&lo->b, NULL);
        } else if (s->lhs) {
            IrOperand v = lower_rvalue(lo, s->lhs);

            ir_build_ret(&lo->b, &v);
        } else {
            ir_build_ret(&lo->b, NULL);
        }
        lo->terminated = true;
        return;
    default:
        return;
    }
}
