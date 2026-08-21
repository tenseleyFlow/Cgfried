#include <string.h>

#include "sema/sema.h"
#include "util/vec.h"

VEC_DECL(SafeNodeVec, AstNode *);
VEC_DECL(SafeTagVec, TagDecl *);

typedef struct {
    u64 first_bit;
    u64 end_bit;
    u64 count;
    u64 stride_bit;
    u32 alternative;
    bool pointer;
} SafeLeaf;

VEC_DECL(SafeLeafVec, SafeLeaf);
VEC_DECL(SafeMemberVec, Member *);

typedef struct {
    TagDecl *tag;
    unsigned mask;
    bool ready;
} SafeTagMask;

VEC_DECL(SafeTagMaskVec, SafeTagMask);

#define SAFE_MAX_FAMILIES 4096u
#define SAFE_MAX_OVERLAP_CHECKS 65536u
#define SAFE_CAT_POINTER 0x1u
#define SAFE_CAT_NONPOINTER 0x2u

typedef struct {
    Sema *sema;
    AstNode *round_trip_anchor;
    SafeNodeVec seen;
    SafeTagVec unsafe_reported;
    bool union_analysis_exhausted;
    u64 union_analysis_work;
} SafeCtx;

static void safe_error(SafeCtx *sc, Span span, const char *message)
{
    sc->sema->nerrors++;
    diag_emit(sc->sema->dc, DIAG_ERROR, span, "%s", message);
}

static AstNode *strip_paren_implicit(AstNode *e)
{
    while (e && (e->kind == AST_EXPR_PAREN ||
                 (e->kind == AST_EXPR_CAST && e->implicit)))
        e = e->lhs;
    return e;
}

static bool ast_type_is_uintptr(const AstType *t)
{
    return t && t->kind == ATY_BASE && t->base == ABT_TYPEDEF &&
           t->typedef_name && strcmp(t->typedef_name, "uintptr_t") == 0;
}

static bool is_integer_constant(SafeCtx *sc, AstNode *e)
{
    ConstValue value;

    e = strip_paren_implicit(e);
    if (!e || !e->sem_type || !type_is_integer(e->sem_type))
        return false;
    value = constexpr_eval(sc->sema, e, CE_FOLD);
    return value.kind == CV_INT;
}

static bool is_char_pointer(const Type *type)
{
    return type && type->kind == TY_PTR && type->base &&
           type->base->kind == TY_CHAR;
}

static bool is_unregistered_allocator(const Symbol *sym)
{
    const char *name;
    const Type *type;

    /* MS-C-06: reject only a compatible external identity whose allocation
     * would bypass runtime registration.  Harmless static or incompatible
     * same-name functions, and external functions renamed away from the
     * allocator identity, remain ordinary calls. */
    if (!sym || sym->kind != SYM_FUNC || sym->linkage != LINK_EXTERNAL)
        return false;
    name = sym->asm_name ? sym->asm_name
                         : (sym->alias_target ? sym->alias_target : sym->name);
    if (!name ||
        (strcmp(name, "asprintf") != 0 && strcmp(name, "vasprintf") != 0))
        return false;
    type = sym->type;
    if (!type || type->kind != TY_FUNC || !type->base ||
        type->base->kind != TY_INT || !type->has_proto || type->nparams < 2 ||
        !type->params[0] || type->params[0]->kind != TY_PTR ||
        !is_char_pointer(type->params[0]->base) ||
        !is_char_pointer(type->params[1]))
        return false;
    return strcmp(name, "asprintf") == 0
               ? type->nparams == 2 && type->variadic
               : type->nparams == 3 && !type->variadic;
}

static bool is_uintptr_anchor(AstNode *e)
{
    e = strip_paren_implicit(e);
    return e && e->kind == AST_EXPR_CAST && !e->implicit &&
           ast_type_is_uintptr(e->type) && e->lhs && e->lhs->sem_type &&
           e->lhs->sem_type->kind == TY_PTR;
}

static bool is_ptr_derived(SafeCtx *sc, AstNode *e)
{
    AstNode *rhs;

    e = strip_paren_implicit(e);
    if (is_uintptr_anchor(e))
        return true;
    if (!e || e->kind != AST_EXPR_BINARY)
        return false;

    /* Optional tag bits: ptr | constant. */
    if (e->op == PUNCT_PIPE && is_uintptr_anchor(e->lhs) &&
        is_integer_constant(sc, e->rhs))
        return true;

    /* Optional alignment mask: ptr-derived & ~constant. */
    rhs = strip_paren_implicit(e->rhs);
    if (e->op == PUNCT_AMP && is_ptr_derived(sc, e->lhs) && rhs &&
        rhs->kind == AST_EXPR_UNARY && rhs->op == PUNCT_TILDE &&
        is_integer_constant(sc, rhs->lhs))
        return true;
    return false;
}

static bool is_uintptr_round_trip(SafeCtx *sc, AstNode *e)
{
    e = strip_paren_implicit(e);
    if (is_ptr_derived(sc, e))
        return true;
    if (!e || e->kind != AST_EXPR_BINARY)
        return false;
    if (e->op != PUNCT_PLUS && e->op != PUNCT_MINUS && e->op != PUNCT_AMP &&
        e->op != PUNCT_PIPE)
        return false;
    return is_ptr_derived(sc, e->lhs) && is_integer_constant(sc, e->rhs);
}

static AstNode *uintptr_anchor(AstNode *e)
{
    e = strip_paren_implicit(e);
    if (is_uintptr_anchor(e))
        return e;
    if (!e || e->kind != AST_EXPR_BINARY)
        return NULL;
    return uintptr_anchor(e->lhs);
}

static bool tag_in_vec(const SafeTagVec *tags, const TagDecl *tag)
{
    size_t i;

    for (i = 0; i < tags->len; i++)
        if (tags->data[i] == tag)
            return true;
    return false;
}

static bool checked_add_u64(u64 a, u64 b, u64 *out)
{
    if (a > UINT64_MAX - b)
        return false;
    *out = a + b;
    return true;
}

static bool checked_mul_u64(u64 a, u64 b, u64 *out)
{
    if (a && b > UINT64_MAX / a)
        return false;
    *out = a * b;
    return true;
}

static void push_leaf(SafeCtx *sc, SafeLeafVec *leaves, SafeLeaf leaf)
{
    if (leaves->len >= SAFE_MAX_FAMILIES) {
        sc->union_analysis_exhausted = true;
        return;
    }
    SafeLeafVec_push(leaves, leaf);
}

static bool union_work_available(SafeCtx *sc)
{
    if (sc->union_analysis_work >= SAFE_MAX_OVERLAP_CHECKS) {
        sc->union_analysis_exhausted = true;
        return false;
    }
    sc->union_analysis_work++;
    return true;
}

static bool category_layout_equal(SafeCtx *sc, Type *a, Type *b);

static bool category_members_equal(SafeCtx *sc, Member *a, Member *b)
{
    if (!a || !b)
        return a == b;
    if (!union_work_available(sc))
        return false;
    if (a->is_bitfield || b->is_bitfield)
        return a->is_bitfield == b->is_bitfield &&
               a->bit_offset == b->bit_offset && a->bit_width == b->bit_width;
    return a->offset == b->offset &&
           category_layout_equal(sc, a->type, b->type);
}

static bool category_layout_equal(SafeCtx *sc, Type *a, Type *b)
{
    Member *ma, *mb;
    TypeLayout la, lb;

    if (!a || !b)
        return a == b;
    if (!union_work_available(sc))
        return false;
    if (a->kind == TY_PTR || b->kind == TY_PTR) {
        if (a->kind != b->kind)
            return false;
        la = layout_of(sc->sema, a);
        lb = layout_of(sc->sema, b);
        return la.size == lb.size;
    }
    if (a->kind == TY_ARRAY || b->kind == TY_ARRAY)
        return a->kind == b->kind && a->has_size == b->has_size &&
               (!a->has_size || a->size == b->size) &&
               category_layout_equal(sc, a->base, b->base);
    if (a->kind == TY_STRUCT || a->kind == TY_UNION || b->kind == TY_STRUCT ||
        b->kind == TY_UNION) {
        if (a->kind != b->kind || !a->tag || !b->tag ||
            a->tag->complete != b->tag->complete)
            return false;
        if (!a->tag->complete)
            return true;
        layout_record(sc->sema, a);
        layout_record(sc->sema, b);
        if (a->tag->size != b->tag->size)
            return false;
        ma = a->tag->members;
        mb = b->tag->members;
        while (ma && mb) {
            if (!category_members_equal(sc, ma, mb))
                return false;
            ma = ma->next;
            mb = mb->next;
        }
        return !ma && !mb;
    }
    la = layout_of(sc->sema, a);
    lb = layout_of(sc->sema, b);
    return la.size == lb.size;
}

static bool alternatives_equal(SafeCtx *sc, Member *a, Member *b)
{
    if (!a || !b || a->is_bitfield != b->is_bitfield)
        return false;
    if (!union_work_available(sc))
        return false;
    if (a->is_bitfield)
        return a->bit_offset == b->bit_offset && a->bit_width == b->bit_width;
    return a->offset == b->offset &&
           category_layout_equal(sc, a->type, b->type);
}

static unsigned type_category_mask_inner(SafeCtx *sc, Type *t,
                                         SafeTagMaskVec *cache)
{
    Member *m;
    unsigned mask = 0;
    size_t entry_index, i;

    if (!t)
        return 0;
    if (t->kind == TY_PTR)
        return SAFE_CAT_POINTER;
    if (t->kind == TY_ARRAY)
        return t->has_size && t->size
                   ? type_category_mask_inner(sc, t->base, cache)
                   : 0;
    if (t->kind == TY_STRUCT || t->kind == TY_UNION) {
        if (!t->tag || !t->tag->complete)
            return 0;
        for (i = 0; i < cache->len; i++) {
            if (!union_work_available(sc))
                return SAFE_CAT_POINTER | SAFE_CAT_NONPOINTER;
            if (cache->data[i].tag == t->tag)
                return cache->data[i].ready
                           ? cache->data[i].mask
                           : SAFE_CAT_POINTER | SAFE_CAT_NONPOINTER;
        }
        if (cache->len >= SAFE_MAX_FAMILIES || !union_work_available(sc))
            return SAFE_CAT_POINTER | SAFE_CAT_NONPOINTER;
        entry_index = cache->len;
        SafeTagMaskVec_push(cache, ((SafeTagMask){t->tag, 0, false}));
        for (m = t->tag->members; m; m = m->next) {
            if (!union_work_available(sc)) {
                mask = SAFE_CAT_POINTER | SAFE_CAT_NONPOINTER;
                break;
            }
            if (m->is_bitfield) {
                if (m->bit_width)
                    mask |= SAFE_CAT_NONPOINTER;
            } else {
                mask |= type_category_mask_inner(sc, m->type, cache);
            }
        }
        cache->data[entry_index].mask = mask;
        cache->data[entry_index].ready = true;
        return mask;
    }
    if (!layout_is_complete_for_size(t) || layout_of(sc->sema, t).size == 0)
        return 0;
    return SAFE_CAT_NONPOINTER;
}

static unsigned alternative_category_mask(SafeCtx *sc, Member *m,
                                          SafeTagMaskVec *cache)
{
    if (!m)
        return 0;
    if (m->is_bitfield)
        return m->bit_width ? SAFE_CAT_NONPOINTER : 0;
    return type_category_mask_inner(sc, m->type, cache);
}

static bool category_masks_cross(unsigned a, unsigned b)
{
    return ((a & SAFE_CAT_POINTER) && (b & SAFE_CAT_NONPOINTER)) ||
           ((b & SAFE_CAT_POINTER) && (a & SAFE_CAT_NONPOINTER));
}

static bool shift_leaf(SafeCtx *sc, SafeLeaf *leaf, u64 offset)
{
    if (!checked_add_u64(leaf->first_bit, offset, &leaf->first_bit) ||
        !checked_add_u64(leaf->end_bit, offset, &leaf->end_bit)) {
        sc->union_analysis_exhausted = true;
        return false;
    }
    return true;
}

static bool leaf_extent_fits(SafeCtx *sc, const SafeLeaf *leaf)
{
    u64 tail, last_end;

    if (leaf->count == 0 || leaf->end_bit < leaf->first_bit) {
        sc->union_analysis_exhausted = true;
        return false;
    }
    if (leaf->count == 1)
        return true;
    if (leaf->stride_bit == 0 ||
        !checked_mul_u64(leaf->count - 1, leaf->stride_bit, &tail) ||
        !checked_add_u64(leaf->end_bit, tail, &last_end)) {
        sc->union_analysis_exhausted = true;
        return false;
    }
    return true;
}

static void push_checked_leaf(SafeCtx *sc, SafeLeafVec *leaves, SafeLeaf leaf)
{
    if (leaf_extent_fits(sc, &leaf))
        push_leaf(sc, leaves, leaf);
}

static void collect_leaves(SafeCtx *sc, Type *t, u64 base_bit, u32 alternative,
                           SafeLeafVec *leaves)
{
    Member *m;
    TypeLayout tl;

    if (sc->union_analysis_exhausted || !t || !layout_is_complete_for_size(t))
        return;
    if (t->kind == TY_ARRAY) {
        SafeLeafVec element = {0};
        u64 elem_bits;
        size_t i;

        if (t->size == 0)
            return;
        tl = layout_of(sc->sema, t->base);
        if (!checked_mul_u64(tl.size, 8, &elem_bits)) {
            sc->union_analysis_exhausted = true;
            return;
        }
        collect_leaves(sc, t->base, 0, alternative, &element);
        if (sc->union_analysis_exhausted) {
            SafeLeafVec_free(&element);
            return;
        }
        for (i = 0; i < element.len; i++) {
            SafeLeaf leaf = element.data[i];
            u64 width = leaf.end_bit - leaf.first_bit;
            u64 merged_span;

            if (leaf.count == 1 && leaf.first_bit == 0 && width == elem_bits) {
                if (!checked_mul_u64(elem_bits, t->size, &leaf.end_bit)) {
                    sc->union_analysis_exhausted = true;
                    break;
                }
                leaf.first_bit = 0;
                if (!shift_leaf(sc, &leaf, base_bit))
                    break;
            } else if (leaf.count > 1 &&
                       checked_mul_u64(leaf.stride_bit, leaf.count,
                                       &merged_span) &&
                       merged_span == elem_bits) {
                if (!checked_mul_u64(leaf.count, t->size, &leaf.count) ||
                    !shift_leaf(sc, &leaf, base_bit))
                    break;
            } else if (leaf.count == 1) {
                leaf.count = t->size;
                leaf.stride_bit = elem_bits;
                if (!shift_leaf(sc, &leaf, base_bit))
                    break;
            } else {
                u64 index;
                u64 available = SAFE_MAX_FAMILIES - leaves->len;

                if (leaf.count <= 1024 && leaf.count <= available) {
                    for (index = 0; index < leaf.count; index++) {
                        SafeLeaf family = leaf;
                        u64 offset;

                        if (!checked_mul_u64(index, leaf.stride_bit, &offset) ||
                            !checked_add_u64(offset, base_bit, &offset) ||
                            !shift_leaf(sc, &family, offset))
                            break;
                        family.count = t->size;
                        family.stride_bit = elem_bits;
                        push_checked_leaf(sc, leaves, family);
                        if (sc->union_analysis_exhausted)
                            break;
                    }
                    if (sc->union_analysis_exhausted)
                        break;
                    continue;
                }
                if (t->size <= 1024 && t->size <= available) {
                    for (index = 0; index < t->size; index++) {
                        SafeLeaf family = leaf;
                        u64 offset;

                        if (!checked_mul_u64(index, elem_bits, &offset) ||
                            !checked_add_u64(offset, base_bit, &offset) ||
                            !shift_leaf(sc, &family, offset))
                            break;
                        push_checked_leaf(sc, leaves, family);
                        if (sc->union_analysis_exhausted)
                            break;
                    }
                    if (sc->union_analysis_exhausted)
                        break;
                    continue;
                }
                /* Widen the inner progression to its bounding interval.
                 * This can reject a safe layout, but it cannot hide an
                 * overlapping pointer/non-pointer byte. */
                if (!checked_mul_u64(leaf.count - 1, leaf.stride_bit,
                                     &merged_span) ||
                    !checked_add_u64(leaf.end_bit, merged_span,
                                     &leaf.end_bit)) {
                    sc->union_analysis_exhausted = true;
                    break;
                }
                leaf.count = t->size;
                leaf.stride_bit = elem_bits;
                if (!shift_leaf(sc, &leaf, base_bit))
                    break;
            }
            push_checked_leaf(sc, leaves, leaf);
            if (sc->union_analysis_exhausted)
                break;
        }
        SafeLeafVec_free(&element);
        return;
    }
    if ((t->kind == TY_STRUCT || t->kind == TY_UNION) && t->tag) {
        layout_record(sc->sema, t);
        for (m = t->tag->members; m; m = m->next) {
            u64 member_bit;

            if (!m->type || !layout_is_complete_for_size(m->type))
                continue;
            if (m->is_bitfield) {
                SafeLeaf leaf;

                if (!m->bit_width)
                    continue;
                if (!checked_add_u64(base_bit, m->bit_offset,
                                     &leaf.first_bit) ||
                    !checked_add_u64(leaf.first_bit, m->bit_width,
                                     &leaf.end_bit)) {
                    sc->union_analysis_exhausted = true;
                    return;
                }
                leaf.count = 1;
                leaf.stride_bit = 0;
                leaf.alternative = alternative;
                leaf.pointer = false;
                push_checked_leaf(sc, leaves, leaf);
            } else {
                if (!checked_mul_u64(m->offset, 8, &member_bit) ||
                    !checked_add_u64(base_bit, member_bit, &member_bit)) {
                    sc->union_analysis_exhausted = true;
                    return;
                }
                collect_leaves(sc, m->type, member_bit, alternative, leaves);
            }
            if (sc->union_analysis_exhausted)
                return;
        }
        return;
    }
    tl = layout_of(sc->sema, t);
    if (tl.size) {
        SafeLeaf leaf;
        u64 width;

        if (!checked_mul_u64(tl.size, 8, &width) ||
            !checked_add_u64(base_bit, width, &leaf.end_bit)) {
            sc->union_analysis_exhausted = true;
            return;
        }
        leaf.first_bit = base_bit;
        leaf.count = 1;
        leaf.stride_bit = 0;
        leaf.alternative = alternative;
        leaf.pointer = t->kind == TY_PTR;
        push_checked_leaf(sc, leaves, leaf);
    }
}

static bool progression_hits(u64 first, u64 step, u64 count, u64 low, u64 high)
{
    u64 delta, index, hit;

    if (low > high || count == 0 || first > high)
        return false;
    if (first >= low)
        return true;
    if (step == 0)
        return false;
    delta = low - first;
    index = delta / step;
    if (delta % step)
        index++;
    if (index >= count || !checked_mul_u64(index, step, &hit) ||
        !checked_add_u64(first, hit, &hit))
        return index < count; /* arithmetic uncertainty rejects safely */
    return hit <= high;
}

static bool single_overlaps_family(u64 single_start, u64 single_width,
                                   const SafeLeaf *family)
{
    u64 family_width = family->end_bit - family->first_bit;
    u64 low, high;

    if (single_width == 0 || family_width == 0)
        return false;
    low = single_start >= family_width - 1 ? single_start - (family_width - 1)
                                           : 0;
    high = single_start > UINT64_MAX - (single_width - 1)
               ? UINT64_MAX
               : single_start + single_width - 1;
    return progression_hits(family->first_bit, family->stride_bit,
                            family->count, low, high);
}

static bool family_last_end(const SafeLeaf *leaf, u64 *last_end)
{
    u64 tail;

    if (leaf->count == 0)
        return false;
    if (leaf->count == 1) {
        *last_end = leaf->end_bit;
        return true;
    }
    return leaf->stride_bit != 0 &&
           checked_mul_u64(leaf->count - 1, leaf->stride_bit, &tail) &&
           checked_add_u64(leaf->end_bit, tail, last_end);
}

static bool leaves_overlap(const SafeLeaf *a, const SafeLeaf *b)
{
    u64 aw = a->end_bit - a->first_bit;
    u64 bw = b->end_bit - b->first_bit;
    u64 i;

    if (a->count == 1)
        return single_overlaps_family(a->first_bit, aw, b);
    if (b->count == 1)
        return single_overlaps_family(b->first_bit, bw, a);
    if (a->count <= 1024) {
        for (i = 0; i < a->count; i++) {
            u64 offset, start;

            if (!checked_mul_u64(i, a->stride_bit, &offset) ||
                !checked_add_u64(a->first_bit, offset, &start))
                return true;
            if (single_overlaps_family(start, aw, b))
                return true;
        }
        return false;
    }
    if (b->count <= 1024) {
        for (i = 0; i < b->count; i++) {
            u64 offset, start;

            if (!checked_mul_u64(i, b->stride_bit, &offset) ||
                !checked_add_u64(b->first_bit, offset, &start))
                return true;
            if (single_overlaps_family(start, bw, a))
                return true;
        }
        return false;
    }
    {
        u64 a_last_end, b_last_end;

        /* A bounding-interval overlap can be a false positive when both
         * families are sparse. That is the intended conservative result
         * once exact bounded enumeration is no longer cheap. */
        if (!family_last_end(a, &a_last_end) ||
            !family_last_end(b, &b_last_end))
            return true;
        return a->first_bit < b_last_end && b->first_bit < a_last_end;
    }
}

static bool union_is_unsafe(SafeCtx *sc, Type *t)
{
    SafeLeafVec leaves = {0};
    SafeMemberVec alternatives = {0};
    SafeTagMaskVec category_cache = {0};
    Member *m;
    size_t ai, aj, i, j;
    bool unsafe = false;

    if (!t || t->kind != TY_UNION || !t->tag || !t->tag->complete)
        return false;
    sc->union_analysis_exhausted = false;
    sc->union_analysis_work = 0;
    layout_record(sc->sema, t);
    for (m = t->tag->members; m; m = m->next) {
        u32 alternative = (u32)alternatives.len;

        SafeMemberVec_push(&alternatives, m);
        if (sc->union_analysis_exhausted)
            continue;
        if (m->is_bitfield) {
            if (m->bit_width) {
                SafeLeaf leaf;

                leaf.first_bit = m->bit_offset;
                if (!checked_add_u64(m->bit_offset, m->bit_width,
                                     &leaf.end_bit)) {
                    sc->union_analysis_exhausted = true;
                    continue;
                }
                leaf.count = 1;
                leaf.stride_bit = 0;
                leaf.alternative = alternative;
                leaf.pointer = false;
                push_checked_leaf(sc, &leaves, leaf);
            }
        } else {
            collect_leaves(sc, m->type, 0, alternative, &leaves);
        }
    }
    for (ai = 0; ai < alternatives.len && !unsafe; ai++) {
        for (aj = ai + 1; aj < alternatives.len; aj++) {
            bool equal = alternatives_equal(sc, alternatives.data[ai],
                                            alternatives.data[aj]);

            if (equal)
                continue;
            if (sc->union_analysis_exhausted) {
                unsigned a_mask = alternative_category_mask(
                    sc, alternatives.data[ai], &category_cache);
                unsigned b_mask = alternative_category_mask(
                    sc, alternatives.data[aj], &category_cache);

                if (sc->union_analysis_work >= SAFE_MAX_OVERLAP_CHECKS ||
                    category_masks_cross(a_mask, b_mask)) {
                    unsafe = true;
                    break;
                }
                continue;
            }
            for (i = 0; i < leaves.len && !unsafe; i++) {
                if (leaves.data[i].alternative != ai)
                    continue;
                for (j = 0; j < leaves.len; j++) {
                    if (leaves.data[j].alternative != aj ||
                        leaves.data[i].pointer == leaves.data[j].pointer)
                        continue;
                    if (!union_work_available(sc) ||
                        leaves_overlap(&leaves.data[i], &leaves.data[j])) {
                        unsafe = true;
                        break;
                    }
                }
            }
        }
    }
    SafeLeafVec_free(&leaves);
    SafeMemberVec_free(&alternatives);
    SafeTagMaskVec_free(&category_cache);
    return unsafe;
}

static TagDecl *type_find_unsafe_union_inner(SafeCtx *sc, Type *t,
                                             SafeTagVec *path)
{
    Member *m;
    u32 i;
    TagDecl *found;

    if (!t)
        return NULL;
    if (t->kind == TY_PTR)
        return t->base && t->base->kind == TY_FUNC
                   ? type_find_unsafe_union_inner(sc, t->base, path)
                   : NULL;
    if (t->kind == TY_ARRAY)
        return type_find_unsafe_union_inner(sc, t->base, path);
    if (t->kind == TY_FUNC) {
        found = type_find_unsafe_union_inner(sc, t->base, path);
        if (found)
            return found;
        for (i = 0; i < t->nparams; i++) {
            found = type_find_unsafe_union_inner(sc, t->params[i], path);
            if (found)
                return found;
        }
        return NULL;
    }
    if ((t->kind != TY_STRUCT && t->kind != TY_UNION) || !t->tag)
        return NULL;
    if (union_is_unsafe(sc, t))
        return t->tag;
    if (tag_in_vec(path, t->tag))
        return NULL;
    SafeTagVec_push(path, t->tag);
    for (m = t->tag->members; m; m = m->next) {
        found = type_find_unsafe_union_inner(sc, m->type, path);
        if (found) {
            path->len--;
            return found;
        }
    }
    path->len--;
    return NULL;
}

static TagDecl *type_find_unsafe_union(SafeCtx *sc, Type *t)
{
    SafeTagVec path = {0};
    TagDecl *found = type_find_unsafe_union_inner(sc, t, &path);

    SafeTagVec_free(&path);
    return found;
}

static void report_unsafe_union(SafeCtx *sc, TagDecl *tag, Span span)
{
    if (!tag || tag_in_vec(&sc->unsafe_reported, tag))
        return;
    SafeTagVec_push(&sc->unsafe_reported, tag);
    safe_error(sc, span,
               "-fsafe rejects use of a union whose pointer member overlaps "
               "a non-pointer member; use a tagged struct with explicit "
               "accessor functions");
}

static bool is_expression_node(AstKind kind)
{
    return kind >= AST_EXPR_INT && kind <= AST_EXPR_OFFSETOF_BASE;
}

static bool is_nonlocal_jump(const char *name)
{
    return name &&
           (strcmp(name, "setjmp") == 0 || strcmp(name, "_setjmp") == 0 ||
            strcmp(name, "sigsetjmp") == 0 ||
            strcmp(name, "__sigsetjmp") == 0 || strcmp(name, "longjmp") == 0 ||
            strcmp(name, "_longjmp") == 0 || strcmp(name, "siglongjmp") == 0 ||
            strcmp(name, "__siglongjmp") == 0 ||
            strcmp(name, "__longjmp") == 0 ||
            strcmp(name, "__longjmp_chk") == 0);
}

static bool node_seen(SafeCtx *sc, AstNode *n)
{
    size_t i;

    for (i = 0; i < sc->seen.len; i++)
        if (sc->seen.data[i] == n)
            return true;
    SafeNodeVec_push(&sc->seen, n);
    return false;
}

static void walk_node(SafeCtx *sc, AstNode *n);

static void walk_type(SafeCtx *sc, AstType *t)
{
    if (!t)
        return;
    walk_type(sc, t->next);
    walk_type(sc, t->atomic_inner);
    walk_node(sc, t->record);
    walk_node(sc, t->array_size);
}

static void walk_node(SafeCtx *sc, AstNode *n)
{
    AstNode *saved_anchor;
    size_t i;

    if (!n || node_seen(sc, n))
        return;
    saved_anchor = sc->round_trip_anchor;

    walk_type(sc, n->type);

    /* A system header may expose an ABI union without making that union part
     * of the safe TU's implementation. Reject the first user-spelled,
     * by-value use instead: objects, function signatures, nested aggregates,
     * expressions, and type-valued _Alignas slots. Pointers stay opaque until
     * a dereference produces the union value. */
    if (!(n->span.origin & SPAN_ORIGIN_SYSTEM_SPELLING)) {
        if (n->kind == AST_RECORD_DECL && n->is_union && n->is_definition &&
            union_is_unsafe(sc, n->sem_type))
            report_unsafe_union(sc, n->sem_type->tag, n->span);
        else if ((n->kind == AST_DECL || n->kind == AST_FUNC_DEF) &&
                 !(n->storage & AST_SC_TYPEDEF))
            report_unsafe_union(sc, type_find_unsafe_union(sc, n->sem_type),
                                n->span);
        else if (is_expression_node(n->kind))
            report_unsafe_union(sc, type_find_unsafe_union(sc, n->sem_type),
                                n->span);
        if (n->kind == AST_EXPR_MEMBER && n->lhs && n->lhs->sem_type) {
            Type *owner = n->lhs->sem_type;

            if (n->is_arrow && owner->kind == TY_PTR)
                owner = owner->base;
            report_unsafe_union(sc, type_find_unsafe_union(sc, owner), n->span);
        }
        report_unsafe_union(sc, type_find_unsafe_union(sc, n->sem_alignas_type),
                            n->span);
    }

    if (n->kind == AST_EXPR_IDENT && n->sym && n->sym->kind == SYM_FUNC &&
        is_nonlocal_jump(n->sym->name))
        safe_error(sc, n->span,
                   "-fsafe rejects setjmp/longjmp control flow; use "
                   "error-code returns or move it to a non-safe TU");
    if (n->kind == AST_EXPR_IDENT && is_unregistered_allocator(n->sym))
        safe_error(sc, n->span,
                   "-fsafe rejects asprintf/vasprintf because their returned "
                   "allocation is not registered by the safe runtime; use "
                   "a wrapped allocation family or move the call to a "
                   "non-safe TU");

    if (n->kind == AST_EXPR_CAST && n->lhs && n->sem_type && n->lhs->sem_type) {
        bool to_pointer = n->sem_type->kind == TY_PTR;
        bool from_pointer = n->lhs->sem_type->kind == TY_PTR;
        bool to_integer = type_is_integer(n->sem_type);
        bool from_integer = type_is_integer(n->lhs->sem_type);

        if (to_pointer && from_integer) {
            if (conv_is_npc(sc->sema, n->lhs)) {
                /* Null pointer constants retain their ISO meaning. */
            } else if (!n->implicit && is_uintptr_round_trip(sc, n->lhs)) {
                /* The complete round trip owns its nested ptr->uintptr
                 * cast. Keep walking its source so a nested rejected call
                 * cannot hide inside an otherwise valid round trip. */
                sc->round_trip_anchor = uintptr_anchor(n->lhs);
            } else {
                AstNode *source = strip_paren_implicit(n->lhs);

                if (source && source->sem_type &&
                    (source->sem_type->quals & CGF_QUAL_VOLATILE))
                    safe_error(sc, n->span,
                               "-fsafe rejects provenance-losing casts "
                               "through volatile or device-I/O integers; "
                               "move the I/O boundary to a non-safe TU");
                else
                    safe_error(sc, n->span,
                               "-fsafe rejects integer-to-pointer casts; use "
                               "the documented uintptr_t round-trip whitelist");
            }
        } else if (from_pointer && to_integer) {
            if (n != sc->round_trip_anchor)
                safe_error(sc, n->span,
                           "-fsafe rejects pointer-to-integer casts outside a "
                           "complete uintptr_t round trip; use the documented "
                           "uintptr_t round-trip whitelist");
        }
    }

    if (n->kind == AST_EXPR_CALL) {
        if (n->op == SEMA_BUILTIN_ALLOCA && n->nargs == 1 &&
            !is_integer_constant(sc, n->args[0]))
            safe_error(sc, n->span,
                       "-fsafe rejects variable-size alloca; use a "
                       "language-scoped VLA or heap allocation");
    }

    walk_node(sc, n->init);
    walk_node(sc, n->alignas_expr);
    walk_node(sc, n->body);
    for (i = 0; i < n->nkr_decls; i++)
        walk_node(sc, n->kr_decls[i]);
    for (i = 0; i < n->nmembers; i++)
        walk_node(sc, n->members[i]);
    walk_node(sc, n->bitfield_width);
    walk_node(sc, n->assert_expr);
    for (i = 0; i < n->nitems; i++)
        walk_node(sc, n->items[i]);
    for (i = 0; i < n->ndesignators; i++)
        walk_node(sc, n->designators[i]);
    walk_node(sc, n->desig_index);
    walk_node(sc, n->lhs);
    walk_node(sc, n->rhs);
    walk_node(sc, n->mid);
    for (i = 0; i < n->nargs; i++)
        walk_node(sc, n->args[i]);
    for (i = 0; i < n->ndecls; i++)
        walk_node(sc, n->decls[i]);
    sc->round_trip_anchor = saved_anchor;
}

void sema_check_safe_mode(Sema *s, AstNode *tu)
{
    SafeCtx sc;

    memset(&sc, 0, sizeof(sc));
    sc.sema = s;
    walk_node(&sc, tu);
    SafeNodeVec_free(&sc.seen);
    SafeTagVec_free(&sc.unsafe_reported);
}
