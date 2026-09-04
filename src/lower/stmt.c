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

/* __builtin_constant_p is deliberately a lowering-time answer rather than a
 * general C integer constant expression: its argument can be an automatic
 * object, and the answer is still the known integer 0.  constexpr_eval() is
 * the source of truth for the ARGUMENT, while this recognizer preserves the
 * builtin's distinct outer contract for a statement condition. */
static bool constant_p_condition(Lower *lo, const AstNode *e, bool *taken)
{
    bool negate = false;
    const AstNode *core;
    ConstValue cv;
    u32 i;

    while (e && (e->kind == AST_EXPR_PAREN ||
                 (e->kind == AST_EXPR_CAST && e->implicit)))
        e = e->lhs;
    while (e && e->kind == AST_EXPR_UNARY && e->op == PUNCT_BANG) {
        negate = !negate;
        e = e->lhs;
        while (e && (e->kind == AST_EXPR_PAREN ||
                     (e->kind == AST_EXPR_CAST && e->implicit)))
            e = e->lhs;
    }
    if (!e || e->kind != AST_EXPR_CALL || e->op != SEMA_BUILTIN_CONSTANT_P ||
        e->nargs != 1)
        return false;
    /* Variadic-pack wrapper specialization may know a named parameter's
     * outer argument even though the parameter has been materialized in a
     * local slot.  Keep this answer in lockstep with lower_rvalue's builtin
     * implementation before consulting the general constant engine. */
    core = e->args[0];
    while (core && (core->kind == AST_EXPR_PAREN ||
                    (core->kind == AST_EXPR_CAST && core->implicit)))
        core = core->lhs;
    if (lo->va_pack && core && core->kind == AST_EXPR_IDENT && core->sym) {
        for (i = 0; i < lo->va_pack->nparams; i++) {
            if (lo->va_pack->params[i] != core->sym)
                continue;
            *taken = lo->va_pack->param_constant[i];
            if (negate)
                *taken = !*taken;
            return true;
        }
    }
    cv = constexpr_eval(lo->sema, e->args[0], CE_FOLD);
    *taken = cv.kind == CV_INT || cv.kind == CV_FLOAT;
    if (negate)
        *taken = !*taken;
    return true;
}

static void defer_config_removal(Lower *lo, BlockId block)
{
    DeferredConfigRemoval *pending = arena_alloc(
        lo->arena, sizeof(*pending), _Alignof(DeferredConfigRemoval));

    pending->block = block;
    pending->next = NULL;
    if (lo->deferred_config_removals_tail)
        lo->deferred_config_removals_tail->next = pending;
    else
        lo->deferred_config_removals = pending;
    lo->deferred_config_removals_tail = pending;
}

void lower_record_deferred_config_removals(Lower *lo)
{
    DeferredConfigRemoval *pending;

    if (lo->failed)
        return;
    for (pending = lo->deferred_config_removals; pending;
         pending = pending->next)
        if (!ir_func_block_reachable(lo->fn, pending->block))
            ir_func_record_removed(lo->fn, pending->block,
                                   IR_CFG_REMOVED_CONFIG);
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

/* Matching private layout in lower.c; label_vla stores these as void*. */
typedef struct VlaLabelState {
    const AstNode *compound;
    const AstNode *checkpoint_decl;
    ValueId token;
    struct VlaLabelState *next;
} VlaLabelState;

/* C17 6.7.6.2p4: the size expression of a VLA type is evaluated when the
 * DECLARATION is reached, whether or not that declaration creates an object
 * of the type. `char (*p)[width]` declares a fixed-size POINTER, yet `p[i]`
 * still has to scale by `width`, so declaration priming walks pointer layers
 * even though dynamic object allocation does not.
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
/* Restores the OUTERMOST live token among the scopes from the innermost
 * up to (but excluding) `stop` — one restore subsumes the inner ones. */
static void vla_restore_until(Lower *lo, LexScope *stop)
{
    LexScope *sc;
    ValueId outer = VALUE_INVALID;

    for (sc = lo->scopes; sc && sc != stop; sc = sc->prev)
        if (sc->token.v)
            outer = sc->token;
    if (outer.v)
        ir_build_stackrestore(&lo->b, ir_op_value(lo->fn, outer));
}

/* A goto restores the outermost scope it leaves, or the earliest VLA
 * checkpoint after the target label in a scope it keeps. */
static void vla_restore_for_goto(Lower *lo, const char *label)
{
    void *hit = strmap_get(&lo->label_vla, label, strlen(label));
    const VlaLabelState *states = hit;
    void *scope_hit = strmap_get(&lo->label_scope, label, strlen(label));
    const LabelScope *target = scope_hit;
    LexScope *sc;
    ValueId outer = VALUE_INVALID;

    for (sc = lo->scopes; sc; sc = sc->prev) {
        const LabelScope *t;
        const VlaLabelState *state;
        bool at_target_scope = false;

        for (t = target; t; t = t->prev)
            if (t->compound == sc->compound) {
                at_target_scope = true;
                break;
            }
        if (!at_target_scope && sc->token.v) {
            outer = sc->token;
            continue;
        }
        for (state = states; state; state = state->next)
            if (state->compound == sc->compound && state->token.v) {
                outer = state->token;
                break;
            }
    }
    if (outer.v)
        ir_build_stackrestore(&lo->b, ir_op_value(lo->fn, outer));
}

/* Fill every label boundary whose first following VLA is this declaration.
 * One stacksave is shared by all such labels and becomes the lexical scope's
 * normal-exit token when it is that scope's first VLA. */
static void vla_checkpoint_decl(Lower *lo, const AstNode *decl_stmt)
{
    StrmapIter it = strmap_iter(&lo->label_vla);
    const char *key;
    size_t key_len;
    void *val;
    ValueId token = VALUE_INVALID;

    while (strmap_iter_next(&it, &key, &key_len, &val)) {
        VlaLabelState *state;

        (void)key;
        (void)key_len;
        for (state = val; state; state = state->next)
            if (state->checkpoint_decl == decl_stmt) {
                if (!token.v)
                    token = ir_build_stacksave(&lo->b);
                state->token = token;
            }
    }
    if (token.v && lo->scopes && !lo->scopes->token.v)
        lo->scopes->token = token;
}

/* --- cleanup scope machinery ------------------------------------------------
 *
 * `cleanup` shares the scope chain with the VLA machinery above and shares
 * NOTHING else. A restore of the outermost token subsumes the inner ones; a
 * cleanup call subsumes nothing, so every scope being left runs every one of
 * its variables. And `return`, which deliberately restores no VLA token
 * because the epilogue reclaims the frame, must run EVERY cleanup — there is
 * no epilogue equivalent for a function call. */

static void cleanup_register(Lower *lo, Symbol *sym, ValueId slot, Span span)
{
    ScopeCleanup *c;

    if (!sym->cleanup_fn || !lo->scopes || !slot.v)
        return;
    c = arena_alloc(lo->arena, sizeof(ScopeCleanup), _Alignof(ScopeCleanup));
    c->slot = slot;
    c->fn = sym->cleanup_fn;
    c->span = span;
    /* PREPEND: walking forward then yields reverse declaration order, which
     * is the order gcc runs them in (measured by execution, not read). */
    c->next = lo->scopes->cleanups;
    lo->scopes->cleanups = c;
}

/* One scope's calls, in list order. The argument is the variable's own slot:
 * `&var` is the alloca, so no address is computed and nothing can have moved
 * it. Sema already proved the call type-checks. */
static void cleanup_run_scope(Lower *lo, const LexScope *sc)
{
    const ScopeCleanup *c;

    for (c = sc->cleanups; c; c = c->next) {
        IrOperand arg = ir_op_value(lo->fn, c->slot);
        u32 fidx;

        ir_builder_set_span(&lo->b, c->span);
        if (lower_internal_func(lo, c->fn, &fidx))
            (void)ir_build_call(&lo->b, IRT_VOID, FUNCREF_INTERNAL, fidx, &arg,
                                1);
        else
            (void)ir_build_call(&lo->b, IRT_VOID, FUNCREF_EXTERNAL,
                                lower_global_sym(lo, c->fn), &arg, 1);
    }
}

/* Every scope from the innermost up to (but excluding) `stop`. Passing NULL
 * leaves the whole function, which is what `return` does. */
static void cleanup_run_until(Lower *lo, const LexScope *stop)
{
    const LexScope *sc;

    for (sc = lo->scopes; sc && sc != stop; sc = sc->prev)
        cleanup_run_scope(lo, sc);
}

/* goto: the scopes actually LEFT are those between here and the innermost
 * compound COMMON to the goto and the label. Scopes the label is still inside
 * are not left, and run later at their own end — measured: a `goto` out of two
 * nested scopes runs those two now and the enclosing one at the end of the
 * function.
 *
 * "Common", not "the label's own", because C permits jumping INTO a cleanup
 * scope (gcc compiles it; see attr_cleanup_jump_in.c). Stopping at the label's
 * innermost compound works only when that compound is on the goto's stack,
 * which a jump inward makes false — and then the walk runs off the top and
 * fires every enclosing cleanup twice. The VLA sibling can take that shortcut
 * because 6.8.6.1p1 forbids the jump that would break it; this cannot. */
static void cleanup_run_for_goto(Lower *lo, const char *label)
{
    void *hit = strmap_get(&lo->label_scope, label, strlen(label));
    const LabelScope *target = hit;
    const LexScope *sc;

    for (sc = lo->scopes; sc; sc = sc->prev) {
        const LabelScope *t;

        for (t = target; t; t = t->prev)
            if (t->compound == sc->compound)
                return; /* the common ancestor: everything from here is kept */
        cleanup_run_scope(lo, sc);
    }
}

/* Both obligations of one scope, in the only order that works: the call
 * receives a pointer into storage the restore is about to release. */
static void scope_exit_here(Lower *lo, const LexScope *sc)
{
    cleanup_run_scope(lo, sc);
    if (sc->token.v)
        ir_build_stackrestore(&lo->b, ir_op_value(lo->fn, sc->token));
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
    g->align = lower_object_align(sym, l.align);
    g->linkage = IRLINK_INTERNAL;
    g->size = l.size;
    if (init) {
        InitImage img;

        if (constexpr_eval_initializer_sized(lo->sema, sym->type, init,
                                             sym->init_storage_size, &img)) {
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

static bool can_elide_unused_fixed_local(Lower *lo, AstNode *d, Symbol *sym)
{
    /* A declaration of a fixed-size automatic object has no runtime effect
     * until an initializer, cleanup, use, assignment, or hardening policy
     * makes its storage observable.  Eliding that otherwise-dead slot also
     * keeps translation-limit probes from manufacturing enormous empty
     * frames merely to hold locals that the source never touches.  VLAs stay
     * live because evaluating their bounds is itself observable. */
    return !d->init && !sym->reads && !sym->writes && !sym->cleanup_fn &&
           lo->auto_var_init == LOWER_AUTO_VAR_INIT_NONE && sym->type &&
           !type_is_runtime_sized(sym->type);
}

static void lower_one_decl(Lower *lo, AstNode *d)
{
    Symbol *sym = d->sym;
    TypeLayout l;
    ValueId slot;

    if (!d->name) {
        /* A GNU tag-only declaration can itself introduce a runtime-sized
         * record (`struct S { char bytes[n]; };`). Its VLA bound is evaluated
         * here even though the declaration creates no ordinary identifier. */
        lower_prime_runtime_sizes(lo, d->sem_type);
        return;
    }
    if (d->storage & AST_SC_TYPEDEF) {
        /* A variably modified typedef evaluates its bounds where the
         * declaration appears, just like a VLA object declaration. It has no
         * storage to allocate, but later sizeof(T) must read this cached
         * extent rather than evaluate the bound again. */
        lower_prime_runtime_sizes(lo, d->sem_type);
        return;
    }
    if (!sym || sym->kind != SYM_VAR)
        return; /* typedef/tag/enum-only declarations */
    lower_prime_runtime_sizes(lo, sym->type);
    if (sym->type && type_is_runtime_sized(sym->type)) {
        /* A VLA or GNU record containing one needs its runtime byte extent,
         * including fixed arrays of such records. The size expressions
         * evaluate exactly ONCE, here, in declaration order (lower_type_size
         * caches per Type node — a later sizeof READS the cache). The
         * scope's stacksave is lazy: emitted at its first dynamic object. */
        IrOperand bytes = lower_type_size(lo, sym->type);
        Type *elem = sym->type;
        TypeLayout el;

        while (elem->kind == TY_ARRAY)
            elem = elem->base;
        el = layout_of(lo->sema, elem);
        if (lo->scopes && !lo->scopes->token.v)
            lo->scopes->token = ir_build_stacksave(&lo->b);
        slot = ir_build_alloca_typed(&lo->b, bytes,
                                     lower_auto_align(sym, el.align),
                                     lower_efftype(lo, sym->type));
        lower_bind_local(lo, sym, slot);
        cleanup_register(lo, sym, slot, d->span);
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
    if (can_elide_unused_fixed_local(lo, d, sym))
        return;

    slot = lower_local_slot(lo, sym);
    if (!slot.v) {
        l = layout_of(lo->sema, sym->type);
        slot = ir_build_alloca_typed(
            &lo->b, lower_i64((i64)(l.size ? l.size : 1)),
            lower_auto_align(sym, l.align), lower_efftype(lo, sym->type));
        lower_bind_local(lo, sym, slot);
    }
    /* AFTER the slot exists and BEFORE the initializer runs. The order
     * matters for neither the call nor the store, but registering here means
     * a variable whose own initializer jumps away still has its cleanup
     * recorded -- which is what the scope exit will look for. */
    cleanup_register(lo, sym, slot, d->span);
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
        type_is_runtime_sized(sym->type) ||
        can_elide_unused_fixed_local(lo, d, sym) ||
        !layout_is_complete_for_size(sym->type) || lower_local_slot(lo, sym).v)
        return;
    l = layout_of(lo->sema, sym->type);
    slot = ir_build_alloca_typed(&lo->b, lower_i64((i64)(l.size ? l.size : 1)),
                                 lower_auto_align(sym, l.align),
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

/* Does this case belong in the IR switch's VALUE TABLE, or is it a range
 * that gets a bounds test instead? THREE loops need this answer -- the one
 * that sizes the table, the one that fills it, and the one that emits the
 * range tests -- and they must agree exactly. They did not on the first
 * draft: the sizing loop learned about ranges and the filling loop did not,
 * so the array was sized for the singles and written with all of them. The
 * overflow corrupted the arena and the IR verifier reported a branch to
 * block id 3894. Hence one predicate, not three conditions. */
static bool case_in_table(const SwitchCase *c)
{
    return !c->is_range;
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
            c->hi = c->value;
            if (s->rhs) {
                ConstValue hv = constexpr_eval(lo->sema, s->rhs, CE_FOLD);

                c->hi = hv.kind == CV_INT ? (i64)hv.i : c->value;
                /* Sema rejected the reversed form; a range that survives
                 * to here is non-empty, and a one-value range is just a
                 * plain label wearing the syntax. */
                c->is_range = c->hi != c->value;
            }
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
        else if (case_in_table(c))
            ncases++;
    }
    if (defblk.v == 0)
        defblk = join; /* no default: fall past the switch */

    /* GNU case RANGES do not enter the IR switch table. Expanding
     * `case 0 ... 1000000:` into a million entries is not an option, and
     * the table is a list of values by construction, so each range becomes
     * one bounds test ahead of the table instead:
     *
     *     d = scrut - lo;  if ((unsigned)d <= hi - lo) goto case;
     *
     * ONE subtract and ONE unsigned compare, whatever the range's width --
     * the wrap is exactly what makes a value below `lo` come out huge and
     * fail. Ranges are tested in SOURCE order, which is observable only if
     * two could match, and sema has already made that an error.
     *
     * Building tables for range-heavy switches remains a post-v0.1.0 codegen
     * optimization; this form is correct at every level and bounded in size. */
    for (c = ctx.cases; c; c = c->next) {
        BlockId next;
        u64 span;
        ValueId d, t;

        if (c->is_default || case_in_table(c))
            continue;
        next = lower_new_block(lo, "sw.range.next");
        span = (u64)c->hi - (u64)c->value;
        d = ir_build2(&lo->b, IR_ISUB, scrut.type, scrut,
                      ir_op_iconst(scrut.type, c->value));
        t = ir_build_icmp(&lo->b, ICMP_ULE, ir_op_value(lo->b.f, d),
                          ir_op_iconst(scrut.type, (i64)span));
        ir_build_condbr(&lo->b, ir_op_value(lo->b.f, t), c->block, NULL, 0,
                        next, NULL, 0);
        lower_at(lo, next);
    }

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
            if (!c->is_default && case_in_table(c)) {
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
    brk.scope_mark = lo->scopes;
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
        LexScope scope;

        scope.token = VALUE_INVALID;
        scope.compound = s;
        scope.cleanups = NULL;
        scope.prev = lo->scopes;
        lo->scopes = &scope;
        for (i = 0; i < s->nitems; i++)
            lower_stmt(lo, s->items[i]);
        /* Normal fall-out: run this scope's cleanups, then rewind its VLAs. */
        if (!lo->terminated)
            scope_exit_here(lo, &scope);
        lo->scopes = scope.prev;
        return;
    }
    case AST_STMT_DECL:
        ensure_open_block(lo, "dead");
        vla_checkpoint_decl(lo, s);
        lower_local_decl(lo, s->lhs);
        return;
    case AST_STMT_ASM:
        ensure_open_block(lo, "dead");
        lower_asm(lo, s);
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
        bool known;
        bool taken;
        bool configuration_dependent;

        ensure_open_block(lo, "dead");
        configuration_dependent = expr_is_configuration_dependent(s->lhs);
        known = constant_p_condition(lo, s->lhs, &taken);
        if (!known)
            c = lower_cond(lo, s->lhs);
        tb = lower_new_block(lo, "if.then");
        join = lower_new_block(lo, "if.join");
        eb = s->rhs ? lower_new_block(lo, "if.else") : join;
        /* `__builtin_constant_p` answers at this lowering point.  Make that
         * answer a real CFG edge now, before target-aware asm constraints are
         * checked.  We nevertheless lower BOTH source arms: a goto or switch
         * case can enter a syntactically untaken arm, and the later
         * reachability cleanup retains exactly those externally entered
         * blocks.  Configuration provenance is recorded separately for an
         * untaken arm, because a direct BR has no false edge to carry it. */
        if (known) {
            ir_build_br(&lo->b, taken ? tb : eb, NULL, 0);
            if (configuration_dependent) {
                mark_config_branch(lo, s->lhs);
                if (s->rhs || !taken)
                    defer_config_removal(lo, taken ? eb : tb);
            }
        } else {
            ir_build_condbr(&lo->b, c, tb, NULL, 0, eb, NULL, 0);
            mark_config_branch(lo, s->lhs);
        }
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
        lc.scope_mark = lo->scopes;
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
        lc.scope_mark = lo->scopes;
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
        LexScope scope;

        ensure_open_block(lo, "dead");
        /* The for-init declaration lives in the FOR statement's own scope,
         * not the enclosing block's: `for (int i C = 1; ...)` runs i's
         * cleanup when the loop ends, before the next statement (measured).
         * The body's compound pushes a second scope inside this one, so a
         * `break` correctly runs the body's cleanups and not the init's —
         * lc.scope_mark is taken AFTER this push for exactly that reason. */
        scope.token = VALUE_INVALID;
        scope.compound = s;
        scope.cleanups = NULL;
        scope.prev = lo->scopes;
        lo->scopes = &scope;
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
        lc.scope_mark = lo->scopes;
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
        /* The loop's exit block is inside the for's scope: a for-init
         * cleanup runs on the way out, however the loop ended. */
        if (!lo->terminated)
            scope_exit_here(lo, &scope);
        lo->scopes = scope.prev;
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

            cleanup_run_for_goto(lo, s->name);
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
            cleanup_run_until(lo, lc->scope_mark);
            vla_restore_until(lo, lc->scope_mark);
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
            cleanup_run_until(lo, lc->scope_mark);
            vla_restore_until(lo, lc->scope_mark);
            ir_build_br(&lo->b, lc->continue_target, NULL, 0);
            lo->terminated = true;
        }
        return;
    }
    case AST_STMT_RETURN: {
        if (lo->va_pack) {
            VaPackContext *ctx = lo->va_pack;
            Type *ret = ctx->return_type;

            /* This return belongs to the specialized wrapper, not the
             * containing IR function. Materialize the source-level result in
             * one slot, leave only the wrapper's scopes, and join the outer
             * call expression. */
            ensure_open_block(lo, "dead");
            if (!ret || ret->kind == TY_VOID) {
                if (s->lhs)
                    (void)lower_rvalue(lo, s->lhs);
            } else if (s->lhs) {
                IrOperand value = lower_rvalue(lo, s->lhs);
                TypeLayout l = layout_of(lo->sema, ret);

                if (lower_is_aggregate(ret)) {
                    lower_memcpy_aggregate(
                        lo, ir_op_value(lo->fn, ctx->return_slot), value, ret,
                        (u32)l.align, lower_aggregate_access_flags(s->lhs));
                } else {
                    ir_build_store_typed(&lo->b, value,
                                         ir_op_value(lo->fn, ctx->return_slot),
                                         (u32)(l.align ? l.align : 1), 0,
                                         lower_efftype(lo, ret));
                }
            } else if (!lower_is_aggregate(ret)) {
                TypeLayout l = layout_of(lo->sema, ret);

                ir_build_store_typed(&lo->b, ir_op_undef(lower_irtype(lo, ret)),
                                     ir_op_value(lo->fn, ctx->return_slot),
                                     (u32)(l.align ? l.align : 1), 0,
                                     lower_efftype(lo, ret));
            }
            cleanup_run_until(lo, ctx->scope_mark);
            vla_restore_until(lo, ctx->scope_mark);
            ir_build_br(&lo->b, ctx->return_target, NULL, 0);
            lo->terminated = true;
            return;
        }
        /* No stackrestore on return: the epilogue's frame teardown
         * subsumes every live VLA token (and longjmp likewise unwinds
         * frames wholesale — tokens die with them). `cleanup` is the
         * opposite: a call has no epilogue equivalent, so every enclosing
         * scope runs here.
         *
         * THE RETURN VALUE IS MATERIALIZED BEFORE THE CLEANUPS RUN, which is
         * observable and was measured rather than assumed: a cleanup that
         * overwrites its variable does NOT change what `return x` returns.
         * That is why this computes an operand in every branch and emits ONE
         * `ret` at the end — with the cleanups spliced between — instead of
         * returning from each branch, where one missed branch would silently
         * skip them. */
        IrOperand rv;
        bool have_rv = false;

        memset(&rv, 0, sizeof(rv));
        ensure_open_block(lo, "dead");
        if (lo->cur_return_type && lo->cur_return_type->kind == TY_VOID) {
            /* Sema already warned about `return expr` here. Preserve its
             * side effects while keeping warning-only IR verifier-valid. */
            if (s->lhs)
                (void)lower_rvalue(lo, s->lhs);
        } else if (!s->lhs) {
            /* A missing value is a warning. Scalar and small-aggregate IR
             * functions nevertheless require a return operand. */
            if (lo->fn->ret != IRT_VOID) {
                rv = ir_op_undef((IrType)lo->fn->ret);
                have_rv = true;
            }
        } else if (lo->sret.v) {
            /* SRET/PAIR aggregate return: memcpy into the hidden result
             * pointer, then ret void (the register story is the
             * IrAbiRet annotation's — see ir.h). */
            IrOperand src = lower_rvalue(lo, s->lhs);
            TypeLayout l = layout_of(lo->sema, s->lhs->sem_type);

            lower_memcpy_aggregate(lo, ir_op_value(lo->fn, lo->sret), src,
                                   s->lhs->sem_type, (u32)l.align,
                                   lower_aggregate_access_flags(s->lhs));
        } else if (lo->cur_abi_ret && lo->cur_abi_ret->kind == ABI_RET_SMALL) {
            /* A small aggregate travels as one wire scalar. Usually that is
             * an eightbyte i64/f64; IR-C-01 also uses an f80 for the exact
             * 16-byte long-double aggregate returned in st0. Load through an
             * 8-byte staging slot only when the object is shorter than its
             * ordinary eightbyte wire value. */
            IrOperand src = lower_rvalue(lo, s->lhs);
            AbiRet *ar = lo->cur_abi_ret;
            IrOperand from = src;
            u8 access_flags = lower_aggregate_access_flags(s->lhs);
            Lvalue lv;

            if (ar->size < 8) {
                ValueId tmp = ir_build_alloca(&lo->b, lower_i64(8), 8);

                lower_memcpy_aggregate(lo, ir_op_value(lo->fn, tmp), src,
                                       s->lhs->sem_type, ar->align,
                                       access_flags);
                from = ir_op_value(lo->fn, tmp);
            }
            memset(&lv, 0, sizeof(lv));
            lv.addr = from;
            lv.unit = ar->small_t;
            lv.align = ar->align > 8 ? ar->align : 8;
            lv.is_volatile =
                ar->size >= 8 && (access_flags & IRF_VOLATILE) != 0;
            rv = lower_load(lo, lv);
            have_rv = true;
        } else {
            rv = lower_rvalue(lo, s->lhs);
            have_rv = true;
        }
        cleanup_run_until(lo, NULL);
        ir_build_ret(&lo->b, have_rv ? &rv : NULL);
        lo->terminated = true;
        return;
    }
    default:
        return;
    }
}

/* A GNU statement expression. Its body is an ordinary compound statement and
 * gets an ordinary LexScope -- what is NOT ordinary is the ORDER at the end:
 *
 *     THE VALUE IS MATERIALIZED BEFORE THE SCOPE'S CLEANUPS RUN.
 *
 * Measured, not assumed. With `int r = tail(({ CLEANUP c = 1; 5; })) + ...`,
 * gcc prints `cleanup(1)` BEFORE `tail(5)`: the 5 is computed, the scope
 * exits, and only then does the enclosing expression continue. Running the
 * cleanups first would hand the caller a value read out of storage the
 * cleanup had already been given a chance to clobber; running them later
 * would leak them past the `}` the programmer wrote.
 *
 * Exactly the shape AST_STMT_RETURN needed for `cleanup`, and for the same
 * reason -- which is why that one computes its operand in every branch and
 * emits ONE `ret` with the cleanups spliced in. */
IrOperand lower_stmt_expr(Lower *lo, AstNode *e)
{
    AstNode *body = e ? e->lhs : NULL;
    AstNode *last = NULL;
    IrOperand v;
    LexScope scope;
    u32 i, n, upto;

    memset(&v, 0, sizeof(v));
    v.kind = IROP_NONE; /* a void `({ ... })` yields nothing */
    if (!body || body->kind != AST_STMT_COMPOUND)
        return v;
    n = body->nitems;
    /* The value is the last item, and ONLY if it is an expression statement:
     * a trailing declaration or a trailing `if` makes the whole thing void,
     * which sema has already typed. */
    if (n && body->items[n - 1] && body->items[n - 1]->kind == AST_STMT_EXPR &&
        body->items[n - 1]->lhs)
        last = body->items[n - 1];
    upto = last ? n - 1 : n;

    scope.token = VALUE_INVALID;
    scope.compound = body;
    scope.cleanups = NULL;
    scope.prev = lo->scopes;
    lo->scopes = &scope;
    for (i = 0; i < upto; i++)
        lower_stmt(lo, body->items[i]);
    if (last && !lo->terminated)
        v = lower_rvalue(lo, last->lhs);
    if (last && !lo->terminated &&
        (lower_aggregate_access_flags(last->lhs) & IRF_VOLATILE)) {
        TypeLayout l = layout_of(lo->sema, e->sem_type);
        ValueId tmp = lower_temp(lo, e->sem_type);

        lower_memcpy_aggregate(lo, ir_op_value(lo->fn, tmp), v, e->sem_type,
                               (u32)l.align, IRF_VOLATILE);
        v = ir_op_value(lo->fn, tmp);
    }
    if (!lo->terminated) {
        scope_exit_here(lo, &scope);
    } else {
        /* A statement expression can end an enclosing expression early with
         * a goto, break, continue, or return.  Its real terminator is
         * already in the CFG; reopen only an orphan continuation so callers
         * can finish lowering their syntactic expression without appending
         * after that terminator.  The final unreachable-block sweep removes
         * the continuation, while a label edge into the expression remains
         * live. */
        if (e->sem_type && e->sem_type->kind != TY_VOID)
            v = ir_op_undef(lower_irtype(lo, e->sem_type));
        ensure_open_block(lo, "dead");
    }
    lo->scopes = scope.prev;
    return v;
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
