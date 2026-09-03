#include "lower/lower.h"

#include <stdio.h>
#include <string.h>

#include "driver/toolchain.h"

/* Module-level lowering: globals from the sema symbol table's end-of-TU
 * decisions (DefKind, the Sprint 16 inline matrix), string literals and
 * file-scope compound literals as anonymous internal globals, and one
 * IrFunc per function definition. Every module this file produces is run
 * through ir_verify by the driver; a failure there is an ICE, never a
 * user error — user errors all ended in sema. */

/* --- small shared helpers ------------------------------------------------- */

IrOperand lower_i64(i64 v)
{
    return ir_op_iconst(IRT_I64, v);
}

bool lower_is_aggregate(const Type *t)
{
    return t &&
           (t->kind == TY_STRUCT || t->kind == TY_UNION || t->kind == TY_ARRAY);
}

#define LOWER_MEM_RANGES_MAX 64u

typedef struct LowerMemRanges {
    IrByteRange ranges[LOWER_MEM_RANGES_MAX];
    u32 n;
    bool suppress;
} LowerMemRanges;

static void lower_mem_range_add(LowerMemRanges *out, u64 lo, u64 hi)
{
    IrByteRange *last;

    if (out->suppress || hi <= lo)
        return;
    if (out->n) {
        last = &out->ranges[out->n - 1];
        if (lo <= last->hi) {
            if (hi > last->hi)
                last->hi = hi;
            return;
        }
    }
    if (out->n == LOWER_MEM_RANGES_MAX) {
        out->suppress = true;
        out->n = 0;
        return;
    }
    out->ranges[out->n++] = (IrByteRange){lo, hi};
}

static void lower_mem_ranges(Lower *lo, Type *t, u64 base, LowerMemRanges *out)
{
    TypeLayout l;

    if (!t || out->suppress)
        return;
    l = layout_of(lo->sema, t);
    switch (t->kind) {
    case TY_STRUCT: {
        Member *m;

        layout_record(lo->sema, t);
        for (m = t->tag->members; m; m = m->next) {
            if (m->is_bitfield) {
                u64 cbits;
                u64 unit;

                if (m->bit_width == 0 || m->container_size == 0)
                    continue;
                if (m->packed) {
                    u64 first = m->bit_offset / 8;
                    u64 last = (m->bit_offset + m->bit_width + 7) / 8;

                    lower_mem_range_add(out, base + first, base + last);
                } else {
                    cbits = m->container_size * 8;
                    unit = (m->bit_offset / cbits) * m->container_size;
                    lower_mem_range_add(out, base + unit,
                                        base + unit + m->container_size);
                }
            } else {
                lower_mem_ranges(lo, m->type, base + m->offset, out);
            }
        }
        return;
    }
    case TY_UNION:
        /* The active member is not retained in the IR.  Requiring every
         * possible union member would manufacture reads that C did not make. */
        out->suppress = true;
        out->n = 0;
        return;
    case TY_ARRAY: {
        TypeLayout el;
        u64 i, count;

        if (!t->has_size || !t->base)
            return;
        el = layout_of(lo->sema, t->base);
        if (!el.size)
            return;
        count = l.size / el.size;
        for (i = 0; i < count && !out->suppress; i++)
            lower_mem_ranges(lo, t->base, base + i * el.size, out);
        return;
    }
    default:
        lower_mem_range_add(out, base, base + l.size);
        return;
    }
}

void lower_memcpy_aggregate(Lower *lo, IrOperand dst, IrOperand src, Type *t,
                            u32 align, u8 flags)
{
    TypeLayout l = layout_of(lo->sema, t);
    LowerMemRanges ranges = {0};

    lower_mem_ranges(lo, t, 0, &ranges);
    ir_mem_layout_register(lo->m, ir_builder_span(&lo->b), l.size,
                           ranges.ranges, ranges.n, ranges.suppress);
    ir_build_memcpy(&lo->b, dst, src, lower_i64((i64)l.size), align, flags);
}

/* IR-H-05: aggregate rvalues are represented by addresses, so the ordinary
 * operand cannot say whether consuming that address performs a volatile access.
 * Keep that source-language fact beside lowering instead of leaking it into
 * IrOperand (whose flags describe call ABI operands). Materialized aggregate
 * values such as calls and ?: temporaries are deliberately nonvolatile; the
 * copy that materializes them carries the selected source's marker. */
u8 lower_aggregate_access_flags(const AstNode *e)
{
    if (!e || !lower_is_aggregate(e->sem_type))
        return 0;
    if (e->is_lvalue && (e->sem_type->quals & CGF_QUAL_VOLATILE))
        return IRF_VOLATILE;
    switch (e->kind) {
    case AST_EXPR_PAREN:
    case AST_EXPR_CAST:
        return lower_aggregate_access_flags(e->lhs);
    case AST_EXPR_BINARY:
        if (e->op == PUNCT_COMMA)
            return lower_aggregate_access_flags(e->rhs);
        if (e->op == PUNCT_ASSIGN)
            return lower_aggregate_access_flags(e->lhs);
        return 0;
    case AST_EXPR_GENERIC:
        return lower_aggregate_access_flags(e->mid);
    case AST_EXPR_CHOOSE_EXPR:
        return lower_aggregate_access_flags(e->choose_taken ? e->mid : e->rhs);
    default:
        return 0;
    }
}

IrType lower_irtype(Lower *lo, const Type *t)
{
    switch (t->kind) {
    case TY_BOOL:
    case TY_CHAR:
    case TY_SCHAR:
    case TY_UCHAR:
        return IRT_I8;
    case TY_SHORT:
    case TY_USHORT:
        return IRT_I16;
    case TY_INT:
    case TY_UINT:
        return IRT_I32;
    case TY_LONG:
    case TY_ULONG:
    case TY_LLONG:
    case TY_ULLONG:
        return IRT_I64;
    case TY_FLOAT:
    case TY_FLOAT32:
        return IRT_F32;
    case TY_DOUBLE:
    case TY_FLOAT64:
    case TY_FLOAT32X:
        return IRT_F64;
    case TY_FLOAT128:
        /* Binary128 on EVERY target, which is exactly how it differs from
         * long double below. No target switch, deliberately. */
        return IRT_F128;
    case TY_LDOUBLE:
    case TY_FLOAT64X:
        /* THE cross-target trap: x87-80 on x86-64, binary128 on
         * arm64-linux, plain double on arm64-macos. One switch on the
         * target table, never a host assumption. */
        switch (cgf_target_layout(lo->sema->target).ldbl_kind) {
        case CGF_LDBL_X87_80:
            return IRT_F80;
        case CGF_LDBL_IEEE128:
            return IRT_F128;
        default:
            return IRT_F64;
        }
    case TY_ENUM: {
        TypeLayout l = layout_of(lo->sema, (Type *)t);

        /* GNU mode(QI/HI) permits an enum representation narrower than int.
         * Preserve that storage width in IR: collapsing it to i32 makes the
         * value disagree with its ABI classification and loses the required
         * sign/zero extension at call boundaries. */
        switch (l.size) {
        case 1:
            return IRT_I8;
        case 2:
            return IRT_I16;
        case 4:
            return IRT_I32;
        case 8:
            return IRT_I64;
        default:
            CGF_ICE("enum has unsupported %llu-byte representation",
                    (unsigned long long)l.size);
        }
    }
    case TY_PTR:
        return IRT_PTR;
    default:
        CGF_ICE("lower_irtype on non-scalar type kind %d", (int)t->kind);
    }
}

EffTypeId lower_efftype(Lower *lo, const Type *t)
{
    if (!t)
        return ETYPE_UNKNOWN;
    /* UNKNOWN is the IR's conservative wildcard. Reusing it here keeps the
     * attribute effective through IR printing/parsing and every optimizer:
     * alias_query never derives a TBAA NoAlias proof from UNKNOWN. */
    if (t->may_alias)
        return ETYPE_UNKNOWN;
    switch (t->kind) {
    case TY_VOID:
    case TY_CHAR:
    case TY_SCHAR:
    case TY_UCHAR:
        return ETYPE_CHAR;
    case TY_BOOL:
        return ETYPE_I8;
    case TY_SHORT:
    case TY_USHORT:
        return ETYPE_I16;
    case TY_INT:
    case TY_UINT:
        return ETYPE_I32;
    case TY_LONG:
    case TY_ULONG:
    case TY_LLONG:
    case TY_ULLONG:
        return ETYPE_I64;
    case TY_FLOAT:
    case TY_FLOAT32:
        return ETYPE_F32;
    case TY_DOUBLE:
    case TY_FLOAT64:
    case TY_FLOAT32X:
        return ETYPE_F64;
    case TY_FLOAT128:
    case TY_LDOUBLE:
    case TY_FLOAT64X:
        switch (lower_irtype(lo, t)) {
        case IRT_F64:
            return ETYPE_F64;
        case IRT_F80:
            return ETYPE_F80;
        default:
            return ETYPE_F128;
        }
    case TY_PTR:
        return ETYPE_PTR;
    case TY_UNION:
        return ETYPE_UNION;
    case TY_STRUCT:
    case TY_ARRAY:
        return ETYPE_AGGREGATE;
    case TY_ENUM:
        switch (lower_irtype(lo, t)) {
        case IRT_I8:
            return ETYPE_I8;
        case IRT_I16:
            return ETYPE_I16;
        case IRT_I32:
            return ETYPE_I32;
        default:
            return ETYPE_I64;
        }
    default:
        return ETYPE_UNKNOWN;
    }
}

BlockId lower_new_block(Lower *lo, const char *prefix)
{
    char buf[64];
    char *name;

    snprintf(buf, sizeof(buf), "%s%u", prefix, lo->fn->nblocks);
    name = arena_strdup(lo->arena, buf);
    return ir_block_new(lo->m, lo->fn, name);
}

void lower_at(Lower *lo, BlockId b)
{
    Span loc = ir_builder_span(&lo->b);

    ir_builder_at(&lo->b, lo->m, lo->fn, b);
    ir_builder_set_span(&lo->b, loc);
    lo->terminated = false;
    lo->dead_region = 0;
}

void lower_unimplemented(Lower *lo, Span span, const char *what, int sprint)
{
    if (!lo->failed)
        diag_emit(lo->dc, DIAG_ERROR, span,
                  "%s is not lowered yet: lands in Sprint %d", what, sprint);
    lo->failed = true;
}

/* --- symbol bookkeeping --------------------------------------------------- */

u32 *lower_u32map_get(const Strmap *map, const char *key, size_t key_len)
{
    return strmap_get(map, key, key_len);
}

void lower_u32map_put(Lower *lo, Strmap *map, const char *key, size_t key_len,
                      u32 value)
{
    u32 *cell = arena_alloc(lo->arena, sizeof(*cell), _Alignof(u32));

    *cell = value;
    strmap_put(map, key, key_len, cell);
}

static u32 *ptrmap_get_u32(const Strmap *map, const void *key)
{
    return lower_u32map_get(map, (const char *)&key, sizeof(key));
}

static void ptrmap_put_u32(Lower *lo, Strmap *map, const void *key, u32 value)
{
    lower_u32map_put(lo, map, (const char *)&key, sizeof(key), value);
}

/* Mach-O normally adds a leading underscore at emission, while a declaration
 * asm label is already the final assembler spelling. Encode only that target's
 * exact names in IR; ELF has no ordinary prefix, so an asm label and an
 * ordinary symbol with the same bytes intentionally denote the same linker
 * symbol there. */
static u32 lower_link_sym_index(Lower *lo, const Symbol *sym)
{
    const char *name = lower_link_name(sym);

    if (sym && sym->asm_name && lo->sema->target.kind == CGF_TARGET_ARM64_MACOS)
        return ir_sym_exact_asm(lo->m, name);
    return ir_sym(lo->m, name);
}

static const char *lower_ir_link_name(Lower *lo, const Symbol *sym)
{
    u32 idx = lower_link_sym_index(lo, sym);

    /* The lookup may grow the arena-backed table, so fetch syms only AFTER
     * it returns. C does not sequence the base expression of `a[f()]` before
     * f(): reading the old table pointer first used a stale allocation. */
    return lo->m->syms[idx];
}

/* Clone source contracts into IR ownership and attach the ABI-level operand
 * index without destroying the source index used by diagnostics. */
const CgfAttr *lower_clone_cgf_attrs(Lower *lo, const CgfAttr *attrs,
                                     const u32 *ir_args, u32 nargs)
{
    CgfAttr *head = NULL;
    CgfAttr **tail = &head;

    for (; attrs; attrs = attrs->next) {
        CgfAttr *copy =
            arena_alloc(lo->m->arena, sizeof(*copy), _Alignof(CgfAttr));

        *copy = *attrs;
        copy->ir_arg =
            attrs->arg && attrs->arg <= nargs ? ir_args[attrs->arg - 1] : 0;
        copy->next = NULL;
        *tail = copy;
        tail = &copy->next;
    }
    return head;
}

/* The module symbol index for a file-scope symbol, interning on first
 * use (externs referenced but never declared as globals still resolve). */
static u32 global_sym_index(Lower *lo, Symbol *sym)
{
    u32 *hit = ptrmap_get_u32(&lo->globals, sym);
    u32 idx;

    if (hit) {
        idx = *hit - 1;
    } else {
        idx = lower_link_sym_index(lo, sym);
        ptrmap_put_u32(lo, &lo->globals, sym, idx + 1);
    }
    ir_sym_set_attrs(lo->m, idx, sym->gnu.weak, sym->gnu.visibility);
    return idx;
}

IrOperand lower_sym_addr(Lower *lo, Symbol *sym)
{
    u32 *hit;

    /* Every caller resolves an identifier before asking lowering for its
     * address.  Keep that invariant explicit: besides producing a useful ICE
     * if a future caller violates it, this closes the impossible null path for
     * the safe-mode analysis that dogfoods the compiler itself. */
    if (!sym)
        CGF_ICE("lower_sym_addr called without a resolved symbol");
    hit = ptrmap_get_u32(&lo->locals, sym);

    if (sym->kind == SYM_FUNC && sym->uses_va_arg_pack) {
        if (!lo->failed)
            diag_emit(lo->dc, DIAG_ERROR, sym->span,
                      "cannot take the address of variadic argument-pack "
                      "wrapper '%s'; it must be called directly",
                      sym->name);
        lo->failed = true;
        return ir_op_undef(IRT_PTR);
    }

    if (hit) {
        ValueId v = {*hit};

        return ir_op_value(lo->fn, v);
    }
    /* A no-linkage object with no local binding means lowering LOST a
     * declaration — fabricating a global here would be a silent
     * miscompile (exactly how the for-init bug hid before this guard).
     * Local statics are exempt: they bind through the globals map. */
    if (sym->linkage == LINK_NONE && sym->kind == SYM_VAR &&
        !ptrmap_get_u32(&lo->globals, sym))
        CGF_ICE("lowering lost the local '%s' (no alloca binding)", sym->name);
    return ir_op_symbol(IRT_PTR, global_sym_index(lo, sym), 0);
}

u32 lower_global_sym(Lower *lo, Symbol *sym)
{
    return global_sym_index(lo, sym);
}

bool lower_internal_func(Lower *lo, Symbol *sym, u32 *index)
{
    u32 *hit = ptrmap_get_u32(&lo->func_ids, sym);

    if (!hit)
        return false;
    *index = *hit - 1;
    return true;
}

void lower_bind_local(Lower *lo, Symbol *sym, ValueId slot)
{
    IrFunc *f = lo->fn;

    ptrmap_put_u32(lo, &lo->locals, sym, slot.v);
    if (f->nlocal_slots == f->cap_local_slots) {
        u32 nc = f->cap_local_slots ? f->cap_local_slots * 2 : 8;
        IrLocalSlot *ns =
            arena_alloc(lo->arena, nc * sizeof(*ns), _Alignof(IrLocalSlot));

        if (f->nlocal_slots)
            memcpy(ns, f->local_slots, f->nlocal_slots * sizeof(*ns));
        f->local_slots = ns;
        f->cap_local_slots = nc;
    }
    f->local_slots[f->nlocal_slots].addr = slot;
    f->local_slots[f->nlocal_slots].name = sym->name;
    f->local_slots[f->nlocal_slots].decl_span = sym->span;
    f->nlocal_slots++;
}

ValueId lower_local_slot(Lower *lo, Symbol *sym)
{
    u32 *hit = ptrmap_get_u32(&lo->locals, sym);

    return hit ? (ValueId){*hit} : VALUE_INVALID;
}

void lower_bind_static(Lower *lo, Symbol *sym, u32 sym_index)
{
    /* Local statics resolve through the GLOBAL map under their mangled
     * symbol index — lower_sym_addr's global path finds them there. The
     * binding is module-lifetime on purpose: the same Symbol may be
     * referenced again after its block closes only via pointers, but a
     * second execution of the block must reuse the same object. */
    ptrmap_put_u32(lo, &lo->globals, sym, sym_index + 1);
}

static IrOperand lower_size_binary(Lower *lo, IrOp op, IrOperand a, IrOperand b)
{
    ValueId v = ir_build2(&lo->b, op, IRT_I64, a, b);

    return ir_op_value(lo->fn, v);
}

static IrOperand lower_size_align_up(Lower *lo, IrOperand value, u64 align)
{
    if (align <= 1)
        return value;
    value = lower_size_binary(lo, IR_IADD, value, lower_i64((i64)align - 1));
    value = lower_size_binary(lo, IR_UDIV, value, lower_i64((i64)align));
    return lower_size_binary(lo, IR_IMUL, value, lower_i64((i64)align));
}

static u64 lower_runtime_type_align(Lower *lo, Type *t);

static u64 lower_runtime_member_align(Lower *lo, const Member *m)
{
    u64 align = lower_runtime_type_align(lo, m->type);

    if (m->packed)
        align = 1;
    if (m->align_override > align)
        align = m->align_override;
    return align;
}

static u64 lower_runtime_type_align(Lower *lo, Type *t)
{
    const Member *m;
    u64 align = 1;

    if (!t)
        return align;
    if (t->kind == TY_ARRAY) {
        align = lower_runtime_type_align(lo, t->base);
    } else if ((t->kind == TY_STRUCT || t->kind == TY_UNION) && t->tag &&
               type_is_runtime_sized(t)) {
        for (m = t->tag->members; m; m = m->next) {
            u64 member_align;

            if (!m->type)
                continue;
            member_align = lower_runtime_member_align(lo, m);
            if (m->is_bitfield) {
                if (m->bit_width == 0) {
                    if (lo->sema->target.kind != CGF_TARGET_ARM64_LINUX)
                        continue;
                    member_align = lower_runtime_type_align(lo, m->type);
                    if (m->align_override > member_align)
                        member_align = m->align_override;
                } else if (!m->name &&
                           lo->sema->target.kind != CGF_TARGET_ARM64_LINUX) {
                    continue;
                }
            }
            if (member_align > align)
                align = member_align;
        }
        if (t->tag->align_override > align)
            align = t->tag->align_override;
    } else {
        align = layout_of(lo->sema, t).align;
    }
    if (t->align_override && (t->align_is_exact || t->align_override > align))
        align = t->align_override;
    return align;
}

static IrOperand lower_runtime_record_size(Lower *lo, Type *t)
{
    const Member *m;
    u64 record_align = lower_runtime_type_align(lo, t);

    if (t->kind == TY_UNION) {
        IrOperand largest = lower_i64(0);

        for (m = t->tag->members; m; m = m->next) {
            IrOperand member_size;
            ValueId larger;
            ValueId selected;

            if (!m->type)
                continue;
            if (m->is_bitfield)
                member_size = lower_i64((i64)(m->bit_width + 7) / 8);
            else
                member_size = lower_type_size(lo, m->type);
            larger = ir_build_icmp(&lo->b, ICMP_UGT, member_size, largest);
            selected = ir_build_select(&lo->b, ir_op_value(lo->fn, larger),
                                       member_size, largest);
            largest = ir_op_value(lo->fn, selected);
        }
        return lower_size_align_up(lo, largest, record_align);
    }
    {
        IrOperand bits = lower_i64(0);

        for (m = t->tag->members; m; m = m->next) {
            u64 member_align;

            if (!m->type)
                continue;
            member_align = lower_runtime_member_align(lo, m);
            if (m->is_bitfield) {
                u64 unit_bits = layout_of(lo->sema, m->type).size * 8;
                u64 width = m->bit_width;

                if (width == 0) {
                    u64 barrier = unit_bits;

                    if (m->align_override * 8 > barrier)
                        barrier = m->align_override * 8;
                    bits = lower_size_align_up(lo, bits, barrier);
                    continue;
                }
                if (m->align_override)
                    bits = lower_size_align_up(lo, bits, m->align_override * 8);
                if (!m->packed && unit_bits) {
                    IrOperand used = lower_size_binary(
                        lo, IR_UREM, bits, lower_i64((i64)unit_bits));
                    IrOperand need = lower_size_binary(lo, IR_IADD, used,
                                                       lower_i64((i64)width));
                    IrOperand aligned =
                        lower_size_align_up(lo, bits, unit_bits);
                    ValueId crosses = ir_build_icmp(&lo->b, ICMP_UGT, need,
                                                    lower_i64((i64)unit_bits));
                    ValueId selected = ir_build_select(
                        &lo->b, ir_op_value(lo->fn, crosses), aligned, bits);

                    bits = ir_op_value(lo->fn, selected);
                }
                bits =
                    lower_size_binary(lo, IR_IADD, bits, lower_i64((i64)width));
                continue;
            }
            bits = lower_size_align_up(lo, bits, 8);
            bits = lower_size_align_up(lo, bits, member_align * 8);
            bits = lower_size_binary(
                lo, IR_IADD, bits,
                lower_size_binary(lo, IR_IMUL, lower_type_size(lo, m->type),
                                  lower_i64(8)));
        }
        bits = lower_size_align_up(lo, bits, 8);
        return lower_size_align_up(
            lo, lower_size_binary(lo, IR_UDIV, bits, lower_i64(8)),
            record_align);
    }
}

/* Runtime byte sizes are evaluated ONCE at declaration and cached by Type
 * node identity (sizeof reads the cache — fixture-pinned with a side-effecting
 * bound). This includes GNU records containing VLA members as well as C VLA
 * arrays. An UNDECLARED runtime-sized type computes fresh at its sizeof. */
IrOperand lower_type_size(Lower *lo, Type *t)
{
    u32 *hit;
    IrOperand n, inner, result;
    ValueId prod;

    if (!t)
        return lower_i64(0);
    if (!type_is_runtime_sized(t)) {
        TypeLayout l = layout_of(lo->sema, t);

        return lower_i64((i64)l.size);
    }
    hit = lower_u32map_get(&lo->vla_sizes, (const char *)&t, sizeof(t));
    if (hit) {
        ValueId v = {*hit};

        return ir_op_value(lo->fn, v);
    }
    if (t->kind == TY_STRUCT || t->kind == TY_UNION) {
        result = lower_runtime_record_size(lo, t);
        if (result.kind != IROP_VALUE)
            result = lower_size_binary(lo, IR_IADD, result, lower_i64(0));
        lower_u32map_put(lo, &lo->vla_sizes, (const char *)&t, sizeof(t),
                         (u32)result.a);
        return result;
    }
    if (!t->is_vla) {
        if (!t->has_size)
            CGF_ICE("lower_type_size reached an incomplete array");
        n = lower_i64((i64)t->size);
        inner = lower_type_size(lo, t->base);
        prod = ir_build2(&lo->b, IR_IMUL, IRT_I64, n, inner);
    } else {
        n = lower_rvalue(lo, t->size_expr);
        n = lower_scalar_convert(lo, n, t->size_expr->sem_type,
                                 type_basic(TY_LONG));
        inner = lower_type_size(lo, t->base);
        prod = ir_build2(&lo->b, IR_IMUL, IRT_I64, n, inner);
    }
    lower_u32map_put(lo, &lo->vla_sizes, (const char *)&t, sizeof(t), prod.v);
    return ir_op_value(lo->fn, prod);
}

void lower_prime_runtime_sizes(Lower *lo, Type *t)
{
    for (; t; t = t->base) {
        if ((t->kind == TY_STRUCT || t->kind == TY_UNION) &&
            type_is_runtime_sized(t)) {
            (void)lower_type_size(lo, t);
            break;
        } else if (t->kind == TY_ARRAY && type_is_runtime_sized(t) &&
                   (t->is_vla || t->has_size)) {
            /* Parameter adjustment preserves an intentionally incomplete
             * outer `[]`. It has no total size to cache, but its inner VLA
             * still evaluates on entry, so descend through that wrapper. */
            (void)lower_type_size(lo, t);
            break;
        } else if (t->kind != TY_ARRAY && t->kind != TY_PTR) {
            break;
        }
    }
}

ValueId lower_temp(Lower *lo, Type *t)
{
    TypeLayout l = layout_of(lo->sema, t);

    lo->ntemps++;
    return ir_build_alloca_typed(&lo->b, lower_i64((i64)(l.size ? l.size : 1)),
                                 (u32)(l.align ? l.align : 1),
                                 lower_efftype(lo, t));
}

/* --- anonymous globals: strings and file-scope compound literals ---------- */

static u32 lower_anon_global(Lower *lo, const AstNode *e);

u32 lower_anon_sym(Lower *lo, const AstNode *e)
{
    return lower_anon_global(lo, e);
}

/* Copies an InitImage into a fresh IrGlobal, resolving each reloc's
 * target (named symbol or nested anonymous object). */
static void global_from_image(Lower *lo, IrGlobal *g, const InitImage *img)
{
    u32 i;

    g->size = img->size;
    g->init = img->bytes;
    if (img->nrelocs) {
        g->relocs = arena_alloc(lo->arena, img->nrelocs * sizeof(IrReloc),
                                _Alignof(IrReloc));
        g->nrelocs = img->nrelocs;
        for (i = 0; i < img->nrelocs; i++) {
            g->relocs[i].offset = img->relocs[i].offset;
            g->relocs[i].addend = img->relocs[i].addend;
            if (img->relocs[i].sym)
                g->relocs[i].symbol = global_sym_index(lo, img->relocs[i].sym);
            else if (img->relocs[i].anon)
                g->relocs[i].symbol =
                    lower_anon_global(lo, img->relocs[i].anon);
            else
                g->relocs[i].symbol = 0;
        }
    }
}

/* A string literal or file-scope compound literal used as an address
 * constant: materialize the anonymous object. */
static u32 lower_anon_global(Lower *lo, const AstNode *e)
{
    if (e->kind == AST_EXPR_STRING)
        return e->is_func_name ? lower_func_name_object(lo, e)
                               : lower_string_lit(lo, e);
    {
        u32 *hit = ptrmap_get_u32(&lo->globals, e);
        char buf[32];
        IrGlobal *g;
        InitImage img;
        TypeLayout l;
        u32 idx;

        if (hit)
            return *hit - 1;
        snprintf(buf, sizeof(buf), ".Lcompound.%u", lo->nlocal_static++);
        g = ir_global_new(lo->m, arena_strdup(lo->arena, buf));
        l = layout_of(lo->sema, e->sem_type);
        g->align = (u32)(l.align ? l.align : 1);
        g->linkage = IRLINK_INTERNAL;
        if (constexpr_eval_initializer(lo->sema, e->sem_type, e->init, &img))
            global_from_image(lo, g, &img);
        else
            g->size = l.size;
        idx = ir_sym(lo->m, g->name);
        ptrmap_put_u32(lo, &lo->globals, e, idx + 1);
        return idx;
    }
}

/* Is THIS OBJECT read-only for the life of the process, so its bytes belong
 * in .rodata rather than .data?
 *
 * The qualifier is looked for through array element types, because 6.7.3p9
 * puts it there: `const char a[4]` is an array (unqualified) OF const char,
 * so reading quals off the array node alone finds nothing and the object
 * lands in writable memory. gcc puts it in .rodata, and an array is the most
 * common const global there is -- a lookup table.
 *
 * It deliberately does NOT recurse into record members: a const member does
 * not make the object const, and gcc agrees, putting
 * `struct { const int x; } s;` in .data. That is the whole rule -- the
 * object's own qualifier and nothing else. */
static bool lower_object_is_const(const Type *t)
{
    while (t && t->kind == TY_ARRAY) {
        if (t->quals & CGF_QUAL_CONST)
            return true;
        t = t->base;
    }
    return t && (t->quals & CGF_QUAL_CONST) != 0;
}

/* An object's alignment is exact when every contributing complete declaration
 * supplies GNU `aligned`; a plain or incomplete declaration keeps the final
 * type alignment as a live floor. `_Alignas` can only raise that result. Sema
 * records both facts while merging redeclarations.
 *
 * It lives in one place because it was previously in none: every object path
 * -- file-scope global, function-local static, automatic slot -- took the
 * TYPE's alignment and dropped the declaration's. `_Alignas(64) int g;`
 * emitted `.p2align 2` and the address was not 64-aligned at run time, with
 * no diagnostic anywhere. */
u32 lower_object_align(const Symbol *sym, u64 natural)
{
    u64 a = natural ? natural : 1;

    if (sym && sym->align_override &&
        (!sym->align_natural_floor || sym->align_override > a))
        a = sym->align_override;

    if (a > CGF_MAX_OBJECT_ALIGN)
        CGF_ICE("lowering received unsupported object alignment %llu",
                (unsigned long long)a);
    return (u32)a;
}

/* Automatic objects carry their complete, semantically validated alignment
 * requirement into IR. Frame lowering preserves it for both fixed-size and
 * dynamic allocas; capping it here would silently weaken a valid _Alignas. */
u32 lower_auto_align(const Symbol *sym, u64 natural)
{
    return lower_object_align(sym, natural);
}

/* The name the LINKER sees. `__asm__("name")` replaces it outright; the C
 * identifier stays for source references and for diagnostics, which is why
 * this is a separate accessor rather than a rewrite of Symbol.name -- an
 * error message must still say what the programmer wrote. */
const char *lower_link_name(const Symbol *sym)
{
    if (!sym)
        return NULL;
    return sym->asm_name ? sym->asm_name : sym->name;
}

/* --- file-scope objects --------------------------------------------------- */

/* A source declaration group keeps its first declarator in the top-level
 * node and the comma-separated siblings in items[]. Sema deliberately walks
 * both; every file-scope lowering pass must do the same or `int a, b;`
 * registers b but never emits it. */
static AstNode *decl_group_at(AstNode *head, u32 index)
{
    return index == 0 ? head : head->items[index - 1];
}

static void lower_one_alias(Lower *lo, Sema *sema, AstNode *d)
{
    Symbol *sym;
    Symbol *target;
    IrAlias *a;
    const char *name;
    const char *target_name;

    if (!d || d->kind != AST_DECL || !d->name)
        return;
    if (d->storage & AST_SC_TYPEDEF)
        return;
    sym = scope_lookup(sema->file_scope, d->name, NS_ORDINARY);
    if (!sym || !sym->alias_target)
        return;
    target = scope_lookup(sema->file_scope, sym->alias_target, NS_ORDINARY);
    name = lower_ir_link_name(lo, sym);
    target_name = target ? lower_ir_link_name(lo, target) : sym->alias_target;
    if (ir_alias_find(lo->m, name))
        return; /* a redeclaration names the same alias once */
    a = ir_alias_new(lo->m, name, target_name);
    a->linkage =
        sym->linkage == LINK_INTERNAL ? IRLINK_INTERNAL : IRLINK_EXTERNAL;
    a->is_weak = sym->gnu.weak;
    a->visibility = sym->gnu.visibility;
}

/* Aliases, objects and functions alike, in ONE pass. An alias is neither an
 * IrGlobal (no storage, no initializer) nor an IrFunc (no body), and the two
 * declaration shapes reach lowering through different loops -- an object alias
 * is a SYM_VAR the global loop sees, a function alias a SYM_FUNC it skips. One
 * pass keyed on `alias_target` is what stops that difference from becoming two
 * half-implementations.
 *
 * Sema has already proved every target is defined in this TU, so nothing here
 * can produce a dangling `.set`. */
static void lower_aliases(Lower *lo, Sema *sema, AstNode *tu)
{
    u32 i;

    for (i = 0; i < tu->ndecls; i++) {
        AstNode *head = tu->decls[i];
        u32 j;

        if (!head || head->kind != AST_DECL)
            continue;
        for (j = 0; j <= head->nitems; j++)
            lower_one_alias(lo, sema, decl_group_at(head, j));
    }
}

static AstNode *find_initializing_decl(AstNode *tu, const char *name)
{
    u32 i;

    for (i = 0; i < tu->ndecls; i++) {
        AstNode *head = tu->decls[i];
        u32 j;

        if (!head || head->kind != AST_DECL)
            continue;
        for (j = 0; j <= head->nitems; j++) {
            AstNode *d = decl_group_at(head, j);

            if (d && d->name == name && d->init)
                return d;
        }
    }
    return NULL;
}

static void lower_global_var(Lower *lo, Symbol *sym, AstNode *init)
{
    IrGlobal *g;
    TypeLayout l;

    if (sym->alias_target)
        return; /* emitted by lower_aliases; occupies no storage of its own */
    if (sym->def_kind == DEF_NONE) {
        /* An extern declaration is referenced through the symbol table and
         * emits no global -- which means a backend asking "is this symbol
         * thread-local?" cannot tell, and answering "no" is the very silent
         * miscompile this work removed. The local-exec model only reaches a
         * definition in THIS translation unit anyway; an extern one needs
         * initial-exec, which is TLS-005. Refuse rather than guess. */
        if (sym->tls && !lo->include_inline_defs)
            lower_unimplemented(
                lo, sym->span,
                "a reference to an extern _Thread_local (the initial-exec "
                "model)",
                51);
        return;
    }
    if (!sym->type || !layout_is_complete_for_size(sym->type))
        return;
    l = layout_of(lo->sema, sym->type);
    g = ir_global_new(lo->m, lower_ir_link_name(lo, sym));
    g->align = lower_object_align(sym, l.align);
    /* _Thread_local is a property of the OBJECT: one copy per thread, in
     * .tdata/.tbss, reached through the thread pointer rather than by an
     * ordinary address. Until Sprint 51 this was a hard error, because
     * lowering it to a plain global is a SILENT MISCOMPILE -- four threads
     * incrementing one a thousand times each printed 1000 where gcc printed
     * 0, with no diagnostic anywhere. See .docs/audits/tls-debt.md. */
    g->is_tls = sym->tls;
    g->is_weak = sym->gnu.weak;
    g->is_used = sym->gnu.used;
    g->is_const = lower_object_is_const(sym->type);
    g->section = sym->section_name;
    g->visibility = sym->gnu.visibility;
    g->linkage =
        sym->linkage == LINK_INTERNAL ? IRLINK_INTERNAL : IRLINK_EXTERNAL;
    switch (sym->def_kind) {
    case DEF_COMMON:
        g->linkage = IRLINK_COMMON;
        g->is_tentative = true;
        g->size = l.size;
        break;
    case DEF_ZERO_INIT:
        g->size = l.size; /* BSS: no init bytes */
        break;
    default: { /* DEF_INIT */
        InitImage img;

        if (init &&
            constexpr_eval_initializer_sized(lo->sema, sym->type, init,
                                             sym->init_storage_size, &img))
            global_from_image(lo, g, &img);
        else
            g->size = l.size;
        break;
    }
    }
    /* The global's symbol index was interned by ir_global_new; record the
     * mapping so expression references resolve to the same index. */
    ptrmap_put_u32(lo, &lo->globals, sym, lower_link_sym_index(lo, sym) + 1);
    ptrmap_put_u32(lo, &lo->emitted_globals, sym, 1);
}

/* --- function lowering ---------------------------------------------------- */

/* Collect every label in the body up front: labels have FUNCTION scope
 * and forward gotos are the norm, so their blocks must exist before any
 * statement lowers. (The same two-pass idea reappears per switch for
 * case labels — see stmt.c — because Duff's device falls into cases from
 * inside statements lowered before the case is reached textually.) */
static bool decl_group_has_vla(const AstNode *d)
{
    u32 i;

    if (!d)
        return false;
    if (d->sem_type) {
        const Type *t = d->sem_type;

        for (; t; t = t->base)
            if (t->kind == TY_ARRAY && t->is_vla)
                return true;
            else if (t->kind != TY_ARRAY)
                break;
    }
    for (i = 0; i < d->nitems; i++)
        if (decl_group_has_vla(d->items[i]))
            return true;
    return false;
}

/* Per-label VLA positions are kept out of lower.h deliberately: they are an
 * implementation detail shared only by this pre-pass and stmt.c's goto
 * emission. Keep the matching definition there byte-for-byte compatible. */
typedef struct VlaLabelState {
    const AstNode *compound;
    const AstNode *checkpoint_decl;
    ValueId token;
    struct VlaLabelState *next;
} VlaLabelState;

typedef struct VlaPosition {
    const AstNode *compound;
    const AstNode *next_decl;
    struct VlaPosition *prev;
} VlaPosition;

/* The label pre-pass also records TWO kinds of enclosing state per label, and
 * the difference between them is load-bearing:
 *
 *   label_vla    one checkpoint per enclosing compound whose next direct
 *                declaration is variably modified. The declaration fills in
 *                its stacksave token when it lowers; a backward goto restores
 *                the outermost applicable token.
 *   label_scope  the whole CHAIN of enclosing lexical scopes, VLA or not. A
 *                goto's CLEANUP calls walk down to the innermost scope common
 *                to that chain and the goto's own scope stack. A for-init
 *                declaration owns a scope too, despite having no compound.
 *
 * Neither shortcut works for cleanups, and each failed differently:
 *
 * - Reusing label_vla is wrong because it is FILTERED to VLA-bearing
 *   compounds, so in a function with no VLA it is NULL for every label and
 *   the walk runs off the top of the stack.
 * - Recording only the innermost compound is wrong because C permits jumping
 *   INTO a cleanup scope, and then the label's compound is not on the goto's
 *   stack at all — so the walk again runs off the top.
 *
 * Both produce the same symptom: an outer cleanup running TWICE, once at the
 * goto and again when its scope really ends. Both were caught by executing a
 * fixture against gcc rather than by reading this code.
 *
 * IR-C-04: compound-only VLA metadata loses the declaration boundary. A
 * label before a VLA and a label after it inhabit the same compound but need
 * different restore states; multiple VLAs need one boundary apiece. Reverse
 * source-order collection records the first declaration after each label. */
static void collect_labels(Lower *lo, AstNode *s, VlaPosition *vla_positions,
                           LabelScope *scope_chain);

/* Labels have function scope, whereas GNU statement expressions are ordinary
 * expression nodes whose bodies contain ordinary statements.  The label
 * pre-pass must therefore cross the expression/statement boundary before
 * lowering starts: a later goto can enter a statement expression nested in a
 * syntactically dead arm.  Walking the expression's structural children here
 * is intentionally narrower than lowering -- it finds only embedded
 * statement bodies and never emits their values or side effects. */
static void collect_labels_expr(Lower *lo, AstNode *e,
                                VlaPosition *vla_positions,
                                LabelScope *scope_chain)
{
    u32 i;

    if (!e)
        return;
    if (e->kind == AST_EXPR_STMT) {
        collect_labels(lo, e->lhs, vla_positions, scope_chain);
        return;
    }
    collect_labels_expr(lo, e->lhs, vla_positions, scope_chain);
    collect_labels_expr(lo, e->mid, vla_positions, scope_chain);
    collect_labels_expr(lo, e->rhs, vla_positions, scope_chain);
    collect_labels_expr(lo, e->init, vla_positions, scope_chain);
    for (i = 0; i < e->nargs; i++)
        collect_labels_expr(lo, e->args[i], vla_positions, scope_chain);
    for (i = 0; i < e->nitems; i++)
        collect_labels_expr(lo, e->items[i], vla_positions, scope_chain);
}

/* A declaration group's siblings share the head node's `items` array.  An
 * initializer can contain a statement expression just like an expression
 * statement can, so inspect every declarator in source order. */
static void collect_labels_decl_group(Lower *lo, AstNode *d,
                                      VlaPosition *vla_positions,
                                      LabelScope *scope_chain)
{
    u32 i;

    if (!d)
        return;
    collect_labels_expr(lo, d->init, vla_positions, scope_chain);
    for (i = 0; i < d->nitems; i++)
        collect_labels_decl_group(lo, d->items[i], vla_positions, scope_chain);
}

static void collect_labels(Lower *lo, AstNode *s, VlaPosition *vla_positions,
                           LabelScope *scope_chain)
{
    u32 i;

    if (!s)
        return;
    switch (s->kind) {
    case AST_STMT_LABEL: {
        char buf[128];
        BlockId b;
        VlaLabelState *head = NULL;
        VlaPosition *pos;

        for (pos = vla_positions; pos; pos = pos->prev)
            if (pos->next_decl) {
                VlaLabelState *state = arena_alloc(
                    lo->arena, sizeof(VlaLabelState), _Alignof(VlaLabelState));

                state->compound = pos->compound;
                state->checkpoint_decl = pos->next_decl;
                state->token = VALUE_INVALID;
                state->next = head;
                head = state;
            }

        snprintf(buf, sizeof(buf), "L.%s", s->name ? s->name : "?");
        b = ir_block_new(lo->m, lo->fn, arena_strdup(lo->arena, buf));
        lower_u32map_put(lo, &lo->labels, s->name, strlen(s->name), b.v);
        strmap_put(&lo->label_vla, s->name, strlen(s->name), head);
        strmap_put(&lo->label_scope, s->name, strlen(s->name),
                   (void *)scope_chain);
        collect_labels(lo, s->body, vla_positions, scope_chain);
        return;
    }
    case AST_STMT_COMPOUND: {
        LabelScope *here =
            arena_alloc(lo->arena, sizeof(LabelScope), _Alignof(LabelScope));
        const AstNode *next_vla = NULL;

        here->compound = s;
        here->prev = scope_chain;
        for (i = s->nitems; i-- > 0;) {
            VlaPosition pos;
            AstNode *item = s->items[i];

            pos.compound = s;
            pos.next_decl = next_vla;
            pos.prev = vla_positions;
            collect_labels(lo, item, &pos, here);
            if (item && item->kind == AST_STMT_DECL &&
                decl_group_has_vla(item->lhs))
                next_vla = item;
        }
        return;
    }
    case AST_STMT_IF:
        collect_labels_expr(lo, s->lhs, vla_positions, scope_chain);
        collect_labels(lo, s->body, vla_positions, scope_chain);
        collect_labels(lo, s->rhs, vla_positions, scope_chain);
        return;
    case AST_STMT_SWITCH:
    case AST_STMT_WHILE:
    case AST_STMT_DO:
        collect_labels_expr(lo, s->lhs, vla_positions, scope_chain);
        collect_labels(lo, s->body, vla_positions, scope_chain);
        return;
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT:
        collect_labels_expr(lo, s->lhs, vla_positions, scope_chain);
        collect_labels_expr(lo, s->rhs, vla_positions, scope_chain);
        collect_labels(lo, s->body, vla_positions, scope_chain);
        return;
    case AST_STMT_FOR: {
        LabelScope *here =
            arena_alloc(lo->arena, sizeof(LabelScope), _Alignof(LabelScope));

        here->compound = s;
        here->prev = scope_chain;
        if (s->lhs && s->lhs->kind == AST_DECL)
            collect_labels_decl_group(lo, s->lhs, vla_positions, here);
        else
            collect_labels(lo, s->lhs, vla_positions, here);
        collect_labels_expr(lo, s->mid, vla_positions, here);
        collect_labels_expr(lo, s->rhs, vla_positions, here);
        collect_labels(lo, s->body, vla_positions, here);
        return;
    }
    case AST_STMT_DECL:
        collect_labels_decl_group(lo, s->lhs, vla_positions, scope_chain);
        return;
    case AST_STMT_EXPR:
    case AST_STMT_RETURN:
        collect_labels_expr(lo, s->lhs, vla_positions, scope_chain);
        return;
    case AST_STMT_ASM:
        for (i = 0; i < s->asm_nops; i++)
            collect_labels_expr(lo, s->asm_ops[i].expr, vla_positions,
                                scope_chain);
        return;
    default:
        return;
    }
}

static void lower_function(Lower *lo, AstNode *def)
{
    Symbol *sym = scope_lookup(lo->sema->file_scope, def->name, NS_ORDINARY);
    Type *ft;
    IrType ptypes[130];
    u64 pannots[130];
    u32 attr_ir_args[64] = {0};
    bool any_annot = false;
    AbiArg plans[64];
    Type *wire_types[64];
    u32 nplans = 0;
    u32 nir_params = 0;
    static AbiRet aret;
    AbiBudget budget;
    bool hidden;
    bool stacked = false;
    u32 i;
    BlockId entry;

    if (!sym || !sym->type || sym->type->kind != TY_FUNC)
        return;
    /* GNU argument packs are source-level inliner operands. The definition
     * is specialized into every direct caller and is never emitted as a
     * standalone body where the pack would have no supplying call site. */
    if (sym->uses_va_arg_pack)
        return;
    /* The Sprint 16 inline matrix: an INL_INLINE_DEF provides no external
     * definition and gcc emits nothing for it without an `extern`
     * declaration. Keep an always_inline body temporarily as mandatory
     * inliner input; the strip pass removes it before backend emission. */
    if (sym->inline_kind == INL_INLINE_DEF && !lo->include_inline_defs &&
        !sym->gnu.always_inline)
        return;
    ft = sym->type;

    /* The SysV plan: return first (a hidden pointer goes in slot 0),
     * then each parameter expands per its classification. */
    abi_classify_ret(lo, ft->base, &aret);
    hidden = aret.kind == ABI_RET_SRET || aret.kind == ABI_RET_PAIR ||
             aret.kind == ABI_RET_HFA;
    memset(pannots, 0, sizeof(pannots));
    if (hidden)
        ptypes[nir_params++] = IRT_PTR;
    abi_budget_init(lo, &budget, &aret);
    /* named_gp/named_fp ARE the budget: va_start seeds its offsets from what
     * the named arguments consumed, which is exactly what the placement walk
     * is already tracking. They used to be a second, parallel count that
     * missed two cases -- a MEMORY-return hidden pointer (rdi on SysV) and,
     * on AAPCS64, the general register an indirect aggregate's pointer
     * occupies. The second made a callee with a large struct parameter seed
     * __gr_offs at -64 where gcc uses -56, so its first anonymous integer
     * came out of x0. The save area itself stored x1-x7 correctly, which is
     * how two sources of truth hide from each other. The assignment is
     * repeated after every parameter AND made here, so that a function with
     * no named parameters at all does not inherit the previous one's. */
    lo->named_gp = budget.gp;
    lo->named_fp = budget.fp;
    for (i = 0; i < (ft->params ? ft->nparams : def->nparam_syms) && i < 64;
         i++) {
        AbiArg *a = &plans[nplans++];
        Type *wire = ft->params           ? ft->params[i]
                     : def->param_syms[i] ? def->param_syms[i]->type
                                          : type_basic(TY_INT);

        attr_ir_args[i] = nir_params + 1;

        /* An old-style definition receives the default-promoted wire
         * type, then converts it back to the declared parameter type on
         * entry. A preceding prototype survives in ft->params and is
         * already the authoritative wire contract. */
        if (!ft->params) {
            if (wire->kind == TY_FLOAT)
                wire = type_basic(TY_DOUBLE);
            else
                wire = conv_promote_type(lo->sema, wire);
        }
        wire_types[nplans - 1] = wire;

        abi_classify_arg(lo, wire, a);
        /* The DEFINITION half of the placement decision. It must run the
         * same sequence as the call site in expr.c: an aggregate that the
         * caller stacked because the bank was full is one the callee has to
         * read off the stack. */
        abi_arg_place(lo, a, &budget, false);
        /* Mirror of the call site: on AAPCS64 a stacked aggregate is
         * eightbyte leaves the callee reads off the stack; on SysV it is
         * the byval pointer form, which already means the same thing. */
        if (a->kind == ABI_ARG_STACK) {
            stacked = true;
            a->kind =
                (u8)(abi_is_aapcs64(lo) ? ABI_ARG_EIGHTBYTES : ABI_ARG_BYVAL);
        } else {
            stacked = false;
        }
        switch (a->kind) {
        case ABI_ARG_SCALAR: {
            IrType st = lower_irtype(lo, wire);

            if (wire->kind == TY_PTR && (wire->quals & CGF_QUAL_RESTRICT)) {
                pannots[nir_params] |= IR_PARAM_RESTRICT;
                any_annot = true;
            }
            ptypes[nir_params++] = st;
            if (type_is_floating(wire))
                budget.fp++;
            else
                budget.gp++;
            break;
        }
        case ABI_ARG_EIGHTBYTES:
        case ABI_ARG_HFA: {
            /* Both travel as N bit-carrying scalars; only the leaf TYPE and
             * stride differ, and a->t[] already carries the types. An HFA's
             * leaves are f32/f64 and land in v0-v3, an eightbyte pair's are
             * i64 and land in x0-x7; abi_arg_place has already charged
             * whichever queue each one spends. */
            u32 k;

            for (k = 0; k < a->n; k++) {
                if (k == 0 && a->even_gp && !stacked) {
                    pannots[nir_params] |= IR_ABI_EVEN_GPR;
                    any_annot = true;
                }
                if (k == 0 && a->stack_align16 && stacked) {
                    pannots[nir_params] |= IR_ABI_STACK_ALIGN16;
                    any_annot = true;
                }
                /* A stacked aggregate's leaves must ALL be marked, or the
                 * callee reads the first few out of registers the caller
                 * never wrote. */
                if (stacked) {
                    pannots[nir_params] |= IR_PARAM_ONSTACK;
                    any_annot = true;
                }
                ptypes[nir_params++] = a->t[k];
            }
            break;
        }
        default: /* BYVAL and STACK */
            /* The annotation lands on IrFunc.param_annots after
             * ir_func_new — a bare ptr param would look like a pointer
             * in the GP queue to codegen, but this one is the ADDRESS
             * OF THE INCOMING STACK COPY (Sprint 23). IR-H-12: a stacked
             * SysV aggregate has already been normalized from STACK to
             * BYVAL here, so preserve placement from the saved fact. */
            pannots[nir_params] = ir_arg_annot(IR_ARG_BYVAL, a->size);
            if (stacked)
                pannots[nir_params] |= IR_PARAM_ONSTACK;
            any_annot = true;
            ptypes[nir_params++] = IRT_PTR;
            break;
        }
        lo->named_gp = budget.gp;
        lo->named_fp = budget.fp;
    }

    lo->fn =
        ir_func_new(lo->m, lower_ir_link_name(lo, sym),
                    aret.kind == ABI_RET_SCALAR  ? lower_irtype(lo, ft->base)
                    : aret.kind == ABI_RET_SMALL ? aret.small_t
                                                 : IRT_VOID,
                    ptypes, nir_params);
    lo->fn->variadic = ft->variadic;
    lo->fn->unprototyped = !ft->has_proto;
    lo->fn->abi_ret = aret.ir_abi;
    lo->fn->abi_ret_n = (u8)aret.n;
    lo->fn->loc = ir_intern_span(lo->m, def->span);
    lo->fn->cgf_attrs =
        lower_clone_cgf_attrs(lo, sym->cgf_attrs, attr_ir_args, nplans);
    if (sym->linkage == LINK_INTERNAL)
        lo->fn->linkage = IRLINK_INTERNAL;
    lo->fn->is_weak = sym->gnu.weak;
    lo->fn->is_used = sym->gnu.used;
    lo->fn->always_inline = sym->gnu.always_inline;
    lo->fn->inline_only = sym->inline_kind == INL_INLINE_DEF;
    lo->fn->section = sym->section_name;
    lo->fn->is_ctor = sym->gnu.constructor;
    lo->fn->is_dtor = sym->gnu.destructor;
    lo->fn->ctor_prio = sym->ctor_prio;
    lo->fn->dtor_prio = sym->dtor_prio;
    /* Mach-O has no .fini_array. clang implements `destructor` there by
     * synthesizing a wrapper that calls __cxa_atexit(f, 0, &__dso_handle) and
     * registering THAT as an initializer -- real machinery, not a dialect
     * difference -- so it is refused until that lands.
     *
     * Refused HERE rather than in the emitter, and the distinction is not
     * cosmetic: the backend's only refusal is CGF_ICE, whose text says "this is
     * a bug in cgfried", which is the wrong thing to tell someone who wrote
     * correct C. Same correction the over-aligned stack object got. */
    if (lo->fn->is_dtor &&
        cgf_target_selected().kind == CGF_TARGET_ARM64_MACOS) {
        if (!lo->failed)
            diag_emit(lo->dc, DIAG_ERROR, def->span,
                      "the 'destructor' attribute is not supported on "
                      "arm64-macos: Mach-O has no .fini_array, and a "
                      "destructor there must be registered with __cxa_atexit "
                      "(docs/gnu-extensions.md)");
        lo->failed = true;
    }
    lo->fn->visibility = sym->gnu.visibility;
    /* `aligned` on a FUNCTION aligns the CODE. Sema folded it into the same
     * field an object's alignment uses, because it is the same attribute in a
     * different position; the backends take the max against their own default
     * padding, so 0 here means "whatever you already do". */
    lo->fn->align = (u32)sym->align_override;
    if (any_annot) {
        lo->fn->param_annots =
            arena_alloc(lo->m->arena, nir_params * sizeof(u64), _Alignof(u64));
        memcpy(lo->fn->param_annots, pannots, nir_params * sizeof(u64));
    }
    strmap_init(&lo->locals);
    strmap_init(&lo->labels);
    lo->loops = NULL;
    lo->switches = NULL;
    lo->scopes = NULL;
    lo->fname = sym->name;
    lo->sret = hidden ? lo->fn->param_vals[0] : VALUE_INVALID;
    lo->cur_abi_ret = &aret;
    lo->cur_functype = ft;
    lo->cur_return_type = ft->base;
    lo->dead_region = 0;
    lo->next_dead_region = 0;
    lo->deferred_asm_immediates = NULL;
    lo->deferred_asm_immediates_tail = NULL;
    lo->deferred_config_removals = NULL;
    lo->deferred_config_removals_tail = NULL;

    entry = ir_block_new(lo->m, lo->fn, "entry");
    /* The function body compound is the OUTERMOST scope, and passing it as
     * the initial scope_chain is what stops a goto to a function-top-level
     * label from running that scope's cleanups: they run when the body itself
     * ends, exactly once. */
    collect_labels(lo, def->body, NULL, NULL);
    ir_builder_set_span(&lo->b, (Span){0});
    lower_at(lo, entry);

    /* Bind parameters through the symbols sema recorded on the def node
     * (their scope is long popped). A scalar parameter is spilled to an
     * alloca so `&p` and assignment work uniformly (mem2reg undoes this
     * in Sprint 30); an eightbyte-class aggregate is REASSEMBLED into a
     * local temp (the body addresses the aggregate; the wire form was
     * scalars); a byval aggregate IS the caller-made copy, so its
     * incoming ptr is the object's address directly. */
    {
        u32 pi = hidden ? 1 : 0;
        u32 plan_i = 0;

        for (i = 0; i < def->nparam_syms && pi < lo->fn->nparams; i++) {
            Symbol *psym = def->param_syms[i];
            AbiArg *a = plan_i < nplans ? &plans[plan_i++] : NULL;
            Type *wire = a ? wire_types[plan_i - 1] : NULL;

            if (!psym) {
                /* Unnamed slot: still consumes its IR params. */
                pi += a && (a->kind == ABI_ARG_EIGHTBYTES ||
                            a->kind == ABI_ARG_HFA)
                          ? a->n
                          : 1;
                continue;
            }
            if (a &&
                (a->kind == ABI_ARG_EIGHTBYTES || a->kind == ABI_ARG_HFA)) {
                /* The mirror of the call site: N incoming scalars are
                 * written back into one local at the LEAF stride. Eight for
                 * an eightbyte pair; the leaf width for an HFA, which is 4
                 * when the leaves are floats. */
                u64 stride = a->kind == ABI_ARG_HFA || a->t[0] == IRT_F128
                                 ? (u64)ir_type_size(a->t[0])
                                 : 8u;
                u64 rounded = (u64)a->n * stride;
                u32 salign = a->align > (u32)stride ? a->align : (u32)stride;
                ValueId slot = ir_build_alloca_typed(
                    &lo->b, lower_i64((i64)rounded), salign,
                    lower_efftype(lo, psym->type));
                u32 k;

                for (k = 0; k < a->n; k++) {
                    IrOperand dst = ir_op_value(lo->fn, slot);

                    if (k) {
                        ValueId p2 =
                            ir_build_ptradd(&lo->b, ir_op_value(lo->fn, slot),
                                            lower_i64((i64)(stride * k)));

                        dst = ir_op_value(lo->fn, p2);
                    }
                    ir_build_store_typed(
                        &lo->b, ir_op_value(lo->fn, lo->fn->param_vals[pi + k]),
                        dst, (u32)stride, 0, lower_efftype(lo, psym->type));
                }
                ptrmap_put_u32(lo, &lo->locals, psym, slot.v);
                pi += a->n;
            } else if (a &&
                       (a->kind == ABI_ARG_BYVAL || a->kind == ABI_ARG_STACK)) {
                /* Both arrive as the ADDRESS of a copy the caller already
                 * made, so the local IS that address and no store happens.
                 * STACK reaching the scalar branch below instead stored the
                 * POINTER into a fresh slot and then read the aggregate out
                 * of it -- silent garbage, caught by the ABI differential. */
                ptrmap_put_u32(lo, &lo->locals, psym, lo->fn->param_vals[pi].v);
                pi++;
            } else {
                TypeLayout l = layout_of(lo->sema, psym->type);
                ValueId slot = ir_build_alloca_typed(
                    &lo->b, lower_i64((i64)l.size), (u32)l.align,
                    lower_efftype(lo, psym->type));
                IrOperand incoming =
                    ir_op_value(lo->fn, lo->fn->param_vals[pi]);

                if (wire &&
                    !type_compatible(conv_strip_quals(lo->sema, wire),
                                     conv_strip_quals(lo->sema, psym->type)))
                    incoming =
                        lower_scalar_convert(lo, incoming, wire, psym->type);

                ir_build_store_typed(&lo->b, incoming,
                                     ir_op_value(lo->fn, slot), (u32)l.align, 0,
                                     lower_efftype(lo, psym->type));
                ptrmap_put_u32(lo, &lo->locals, psym, slot.v);
                pi++;
            }
        }
    }

    /* 6.9.1p10: parameter array size expressions execute on entry, after
     * the incoming parameter values are bound and in declaration order.
     * The body symbols carry adjusted pointer types, so sema preserves the
     * original declaration layers separately for this evaluation. */
    for (i = 0; i < def->nparam_syms; i++)
        if (def->param_decl_types && def->param_decl_types[i])
            lower_prime_runtime_sizes(lo, def->param_decl_types[i]);

    lower_prebind_locals(lo, def->body);
    lower_stmt(lo, def->body);

    /* Falling off the end: main returns 0 (5.1.2.2.3), void returns, and
     * any other function returns an unspecified value — undef, exactly
     * the Sprint 17 contract for "the caller must not look". */
    if (!lo->terminated) {
        if (sym->is_main && ft->base && ft->base->kind == TY_INT) {
            IrOperand z = ir_op_iconst(lower_irtype(lo, ft->base), 0);

            ir_build_ret(&lo->b, &z);
        } else if (lo->fn->ret == IRT_VOID) {
            ir_build_ret(&lo->b, NULL);
            if (ft->base && ft->base->kind != TY_VOID)
                ir_ret_mark_implicit(&lo->b);
        } else {
            IrOperand u = ir_op_undef((IrType)lo->fn->ret);

            ir_build_ret(&lo->b, &u);
            ir_ret_mark_implicit(&lo->b);
        }
    }

    /* Inline asm immediate constraints are target-aware lowering facts, but
     * their diagnostics belong only to blocks the final source CFG can reach.
     * This must run after every goto/case edge exists and before codegen. */
    lower_record_deferred_config_removals(lo);
    lower_asm_validate_deferred_immediates(lo);
    ir_func_remove_unreachable_with_log(lo->fn);
    /* Lowering fills join blocks after later-created ones, so creation
     * order != document order; renumber so the printed module reparses
     * id-for-id (the -emit-ir self-check demands it). */
    ir_func_renumber(lo->arena, lo->fn);
    strmap_free(&lo->locals);
    strmap_free(&lo->labels);

    if (lo->verify_each && !lo->failed) {
        if (!ir_verify(lo->dc, lo->m))
            CGF_ICE("lowering of '%s' produced IR the verifier rejects "
                    "(CGF_VERIFY_AFTER_EACH)",
                    sym->name);
    }
}

/* --- translation unit ----------------------------------------------------- */

static IrModule *lower_translation_unit_impl(Arena *arena, DiagCtx *dc,
                                             Sema *sema, AstNode *tu,
                                             bool include_inline_defs,
                                             const LowerOptions *options)
{
    Lower lo;
    u32 i;
    const char *ve;

    memset(&lo, 0, sizeof(lo));
    lo.arena = arena;
    lo.dc = dc;
    lo.sema = sema;
    lo.include_inline_defs = include_inline_defs;
    lo.auto_var_init =
        options ? (u8)options->auto_var_init : LOWER_AUTO_VAR_INIT_NONE;
    lo.safe_pointer_checks = options && options->safe_pointer_checks;
    lo.m = ir_module_new(arena, dc);
    strmap_init(&lo.globals);
    strmap_init(&lo.emitted_globals);
    strmap_init(&lo.func_ids);
    strmap_init(&lo.string_pool);
    strmap_init(&lo.func_name_objects);
    strmap_init(&lo.vla_sizes);
    strmap_init(&lo.label_vla);
    strmap_init(&lo.label_scope);
    ve = cgf_env("CGF_VERIFY_AFTER_EACH");
    lo.verify_each = ve && strcmp(ve, "1") == 0;

    /* Pass 1: file-scope objects in declaration order (deterministic
     * symbol table). The DEF_INIT initializer may sit on any one of the
     * symbol's declarations, so pair symbols with their initializing
     * decl first. */
    lower_aliases(&lo, sema, tu);
    /* File-scope basic asm, in SOURCE order. gcc emits the text verbatim
     * between the functions it sits between; we emit it all before them,
     * which is the same thing for every use it has (a symbol definition, an
     * entry stub) and avoids threading source position through emission. */
    for (i = 0; i < tu->ndecls; i++)
        if (tu->decls[i] && tu->decls[i]->kind == AST_STMT_ASM)
            ir_module_add_file_asm(
                lo.m, tu->decls[i]->asm_tmpl ? tu->decls[i]->asm_tmpl : "");
    for (i = 0; i < tu->ndecls; i++) {
        AstNode *head = tu->decls[i];
        u32 di;

        if (!head || head->kind != AST_DECL)
            continue;
        for (di = 0; di <= head->nitems; di++) {
            AstNode *d = decl_group_at(head, di);
            Symbol *sym;

            if (!d || !d->name || (d->storage & AST_SC_TYPEDEF))
                continue;
            sym = scope_lookup(sema->file_scope, d->name, NS_ORDINARY);
            if (!sym || sym->kind != SYM_VAR)
                continue;
            if (ptrmap_get_u32(&lo.emitted_globals, sym))
                continue; /* already emitted (redeclaration) */
            if (sym->def_kind == DEF_INIT && !d->init) {
                AstNode *withinit = find_initializing_decl(tu, d->name);

                lower_global_var(&lo, sym, withinit ? withinit->init : NULL);
            } else {
                lower_global_var(&lo, sym, d->init);
            }
        }
    }

    /* Pass 2a: pre-assign IrFunc indices in definition order so a call
     * lowered in an EARLIER function can be FUNCREF_INTERNAL to a later
     * one — the index is knowable before the body exists. */
    {
        u32 fidx = 0;

        for (i = 0; i < tu->ndecls; i++) {
            AstNode *d = tu->decls[i];
            Symbol *sym;

            if (!d || d->kind != AST_FUNC_DEF || !d->name)
                continue;
            sym = scope_lookup(sema->file_scope, d->name, NS_ORDINARY);
            if (!sym || !sym->type || sym->type->kind != TY_FUNC)
                continue;
            if (sym->uses_va_arg_pack)
                continue;
            if (sym->inline_kind == INL_INLINE_DEF && !include_inline_defs &&
                !sym->gnu.always_inline)
                continue; /* not emitted; calls go through the symbol */
            ptrmap_put_u32(&lo, &lo.func_ids, sym, ++fidx);
        }
    }

    /* Pass 2b: function definitions, in definition order. */
    for (i = 0; i < tu->ndecls; i++) {
        AstNode *d = tu->decls[i];

        if (d && d->kind == AST_FUNC_DEF && d->name)
            lower_function(&lo, d);
    }

    strmap_free(&lo.globals);
    strmap_free(&lo.emitted_globals);
    strmap_free(&lo.func_ids);
    strmap_free(&lo.string_pool);
    strmap_free(&lo.func_name_objects);
    strmap_free(&lo.vla_sizes);
    strmap_free(&lo.label_vla);
    strmap_free(&lo.label_scope);
    if (!lo.failed)
        ir_module_canonicalize_asms(lo.m);
    return lo.failed ? NULL : lo.m;
}

IrModule *lower_translation_unit(Arena *arena, DiagCtx *dc, Sema *sema,
                                 AstNode *tu)
{
    return lower_translation_unit_impl(arena, dc, sema, tu, false, NULL);
}

IrModule *lower_translation_unit_with_options(Arena *arena, DiagCtx *dc,
                                              Sema *sema, AstNode *tu,
                                              const LowerOptions *options)
{
    return lower_translation_unit_impl(arena, dc, sema, tu, false, options);
}

IrModule *lower_translation_unit_for_flow(Arena *arena, DiagCtx *dc, Sema *sema,
                                          AstNode *tu)
{
    return lower_translation_unit_impl(arena, dc, sema, tu, true, NULL);
}
