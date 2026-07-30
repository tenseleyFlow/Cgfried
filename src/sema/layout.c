#include <string.h>

#include "sema/sema.h"

/* Object layout, bitfields, and SysV x86-64 classification.
 *
 * Everything here is a PURE function of (Type, TargetSpec). No host
 * question is ever asked — `long double` is 16 bytes on x86-64 and 8 on
 * arm64-macos, and a host-conditional would silently produce a compiler
 * that folds `sizeof` correctly only when host == target.
 * scripts/check_sema_target.sh enforces that.
 *
 * The bitfield rules are the swamp. They are specified across the psABI
 * and folklore, so the algorithm below is written to match gcc's OBSERVED
 * layout, and the differential generator proves it by asking gcc to
 * accept _Static_assert(offsetof(...) == N) built from our own numbers. */

static u64 align_up(u64 v, u64 a)
{
    if (a == 0)
        return v;
    return (v + a - 1) / a * a;
}

bool layout_is_complete_for_size(const Type *t)
{
    if (!t)
        return false;
    switch (t->kind) {
    case TY_VOID:
    case TY_FUNC:
        return false; /* neither has a size in ISO C */
    case TY_ARRAY:
        return t->has_size && layout_is_complete_for_size(t->base);
    case TY_STRUCT:
    case TY_UNION:
    case TY_ENUM:
        return t->tag && t->tag->complete;
    default:
        return true;
    }
}

static TypeLayout basic_layout(Sema *s, const Type *t)
{
    IntWidths w = cgf_target_int_widths(s->target);
    TargetLayout tl = cgf_target_layout(s->target);
    TypeLayout r;

    switch (t->kind) {
    case TY_BOOL:
        r.size = 1;
        r.align = 1;
        return r;
    case TY_CHAR:
    case TY_SCHAR:
    case TY_UCHAR:
        r.size = w.char_bits / 8;
        r.align = r.size;
        return r;
    case TY_SHORT:
    case TY_USHORT:
        r.size = w.short_bits / 8;
        r.align = r.size;
        return r;
    case TY_INT:
    case TY_UINT:
        r.size = w.int_bits / 8;
        r.align = r.size;
        return r;
    case TY_LONG:
    case TY_ULONG:
        r.size = w.long_bits / 8;
        r.align = r.size;
        return r;
    case TY_LLONG:
    case TY_ULLONG:
        r.size = w.llong_bits / 8;
        r.align = r.size;
        return r;
    case TY_FLOAT:
        r.size = 4;
        r.align = 4;
        return r;
    case TY_DOUBLE:
        r.size = 8;
        r.align = 8;
        return r;
    case TY_LDOUBLE:
        /* THE cross-target trap: 16/16 on x86-64 (x87 80-bit stored in 16
         * bytes) and on arm64-linux (IEEE binary128), but 8/8 on
         * arm64-macos where Apple makes long double the same as double. */
        r.size = tl.ldbl_size;
        r.align = tl.ldbl_align;
        return r;
    case TY_PTR:
        r.size = tl.ptr_size;
        r.align = tl.ptr_align;
        return r;
    default:
        r.size = 1;
        r.align = 1;
        return r;
    }
}

/* --- records ------------------------------------------------------------- */

/* Bit width of a bitfield's DECLARED type — the unit its allocation is
 * measured in. */
static u64 declared_bits(Sema *s, Type *t)
{
    return layout_of(s, t).size * 8;
}

static void layout_struct(Sema *s, TagDecl *tag)
{
    Member *m;
    u64 offset_bits = 0; /* running position, in BITS */
    u64 align = 1;

    for (m = tag->members; m; m = m->next) {
        TypeLayout ml;
        u64 malign;

        if (!m->type || !layout_is_complete_for_size(m->type)) {
            /* A flexible array member has no size and contributes none;
             * anything else incomplete was already diagnosed. */
            m->offset = align_up(offset_bits, 8) / 8;
            m->laid_out = true;
            continue;
        }
        ml = layout_of(s, m->type);
        malign = ml.align;

        if (m->is_bitfield) {
            u64 unit_bits = declared_bits(s, m->type);
            u64 width = m->bit_width;

            m->container_size = ml.size;
            if (width == 0) {
                /* `T : 0` forces the NEXT field to the next boundary of
                 * T's alignment. An unnamed bitfield — which :0 always is
                 * — never affects the struct's own alignment. */
                offset_bits = align_up(offset_bits, unit_bits);
                m->bit_offset = offset_bits;
                m->offset = offset_bits / 8;
                m->laid_out = true;
                continue;
            }
            /* Rule 1: place at the current bit offset unless the field
             * would STRADDLE a boundary of its declared type — that is,
             * unless it fits in what remains of the current declared-type
             * -sized, declared-type-aligned window. This is the rule naive
             * implementations miss: in `struct { char a:7; int b:25; }`
             * the int window is bytes 0-3, bits 7..31 are free, and 25
             * fits — so b lands at bit 7, NOT at bit 32. */
            {
                u64 window_start = offset_bits / unit_bits * unit_bits;
                u64 used_in_window = offset_bits - window_start;

                if (used_in_window + width > unit_bits)
                    offset_bits = align_up(offset_bits, unit_bits);
            }
            m->bit_offset = offset_bits;
            m->offset = offset_bits / 8;
            offset_bits += width;
            /* Rule 3: only a NAMED bitfield imposes its declared type's
             * alignment on the struct. */
            if (m->name && malign > align)
                align = malign;
            m->laid_out = true;
            continue;
        }

        /* An ordinary member starts at the next byte boundary that
         * satisfies its own alignment. */
        offset_bits = align_up(align_up(offset_bits, 8), malign * 8);
        m->offset = offset_bits / 8;
        m->bit_offset = offset_bits;
        m->bit_width = 0;
        m->container_size = ml.size;
        offset_bits += ml.size * 8;
        if (malign > align)
            align = malign;
        m->laid_out = true;
    }

    if (tag->align_override > align)
        align = tag->align_override;
    tag->align = align;
    /* Tail padding is part of sizeof: `struct { long l; char c; }` is 16,
     * not 9, and the array stride is always exactly sizeof. */
    tag->size = align_up(align_up(offset_bits, 8) / 8, align);
    if (tag->size == 0)
        tag->size = align; /* only reachable for a FAM-only shape */
}

static void layout_union(Sema *s, TagDecl *tag)
{
    Member *m;
    u64 size = 0;
    u64 align = 1;

    for (m = tag->members; m; m = m->next) {
        TypeLayout ml;

        m->offset = 0;
        m->bit_offset = 0;
        m->laid_out = true;
        if (!m->type || !layout_is_complete_for_size(m->type))
            continue;
        ml = layout_of(s, m->type);
        m->container_size = ml.size;
        if (m->is_bitfield) {
            /* Every union member starts at bit 0. A ZERO-WIDTH bitfield
             * occupies no storage at all — it exists only to force the
             * next field to a boundary, and in a union there is no "next
             * field" — so it must not claim a whole container. (Found by
             * the layout differential: a union with a `:0` member came
             * out 8 bytes instead of 4.) */
            /* Only the BITS are storage; the declared type governs
             * alignment and the straddle rule, not a mandatory whole
             * container. `union { unsigned long :7; int x; }` is 4 bytes,
             * not 8 — the second finding from the layout differential. */
            u64 bytes = (m->bit_width + 7) / 8;

            if (bytes > size)
                size = bytes;
            if (m->name && ml.align > align)
                align = ml.align;
            continue;
        }
        if (ml.size > size)
            size = ml.size;
        if (ml.align > align)
            align = ml.align;
    }
    if (tag->align_override > align)
        align = tag->align_override;
    tag->align = align;
    tag->size = align_up(size, align);
    if (tag->size == 0)
        tag->size = align;
}

void layout_record(Sema *s, Type *rec)
{
    TagDecl *tag;

    if (!rec || !rec->tag)
        return;
    tag = rec->tag;
    if (!tag->complete)
        return;
    /* Memoized per (tag, target): the same tag laid out for a different
     * target must be recomputed, or a cross-compile inherits the host's
     * answer. */
    if (tag->laid_out && tag->laid_out_for == s->target.kind)
        return;
    tag->laid_out = true;
    tag->laid_out_for = s->target.kind;
    if (tag->kind == TY_UNION)
        layout_union(s, tag);
    else
        layout_struct(s, tag);
}

TypeLayout layout_of(Sema *s, Type *t)
{
    TypeLayout r;

    r.size = 1;
    r.align = 1;
    if (!t)
        return r;

    switch (t->kind) {
    case TY_ARRAY: {
        TypeLayout el = layout_of(s, t->base);

        /* The stride is exactly sizeof(element) — tail padding included,
         * which is why sizeof and the stride can never diverge. */
        r.size = el.size * (t->has_size ? t->size : 0);
        r.align = el.align;
        return r;
    }
    case TY_STRUCT:
    case TY_UNION:
        if (!t->tag || !t->tag->complete)
            return r;
        layout_record(s, t);
        r.size = t->tag->size;
        r.align = t->tag->align;
        return r;
    case TY_ENUM:
        /* An enum has the size of its compatible integer type. */
        return basic_layout(s, t->tag && t->tag->enum_underlying
                                   ? t->tag->enum_underlying
                                   : type_basic(TY_INT));
    default:
        return basic_layout(s, t);
    }
}

u64 layout_offsetof(Sema *s, Type *rec, const Member *m)
{
    layout_record(s, rec);
    return m ? m->offset : 0;
}

/* --- SysV x86-64 classification (psABI 3.2.3) ---------------------------- */

/* The pairwise merge, verbatim from the psABI. Order matters: MEMORY
 * poisons, INTEGER dominates anything that is not MEMORY, and the x87
 * classes inside an aggregate always end in MEMORY. */
static AbiClass merge(AbiClass a, AbiClass b)
{
    if (a == b)
        return a;
    if (a == ABI_NO_CLASS)
        return b;
    if (b == ABI_NO_CLASS)
        return a;
    if (a == ABI_MEMORY || b == ABI_MEMORY)
        return ABI_MEMORY;
    if (a == ABI_INTEGER || b == ABI_INTEGER)
        return ABI_INTEGER;
    if (a == ABI_X87 || a == ABI_X87UP || a == ABI_COMPLEX_X87 ||
        b == ABI_X87 || b == ABI_X87UP || b == ABI_COMPLEX_X87)
        return ABI_MEMORY;
    return ABI_SSE;
}

/* Classifies `t` at byte `off` within the argument, merging into the
 * eightbyte array. A member lands in every eightbyte its byte range
 * intersects — a `char[16]` therefore merges into both. */
static void classify_into(Sema *s, Type *t, u64 off, AbiClass cls[2],
                          int neightbytes)
{
    TypeLayout tl;
    int idx;

    if (!t)
        return;
    tl = layout_of(s, t);

    switch (t->kind) {
    case TY_STRUCT:
    case TY_UNION: {
        Member *m;

        if (!t->tag || !t->tag->complete)
            return;
        layout_record(s, t);
        /* A UNION merges every member into the SAME eightbytes, which is
         * what makes `union { double d; long l; }` come out INTEGER. */
        for (m = t->tag->members; m; m = m->next)
            if (m->type && layout_is_complete_for_size(m->type))
                classify_into(s, m->type, off + m->offset, cls, neightbytes);
        return;
    }
    case TY_ARRAY: {
        u64 i;
        TypeLayout el = layout_of(s, t->base);

        for (i = 0; i < t->size && el.size; i++)
            classify_into(s, t->base, off + i * el.size, cls, neightbytes);
        return;
    }
    default:
        break;
    }

    idx = (int)(off / 8);
    if (idx < 0 || idx >= neightbytes)
        return;

    if (t->kind == TY_LDOUBLE &&
        cgf_target_layout(s->target).ldbl_kind == CGF_LDBL_X87_80) {
        cls[idx] = merge(cls[idx], ABI_X87);
        if (idx + 1 < neightbytes)
            cls[idx + 1] = merge(cls[idx + 1], ABI_X87UP);
        return;
    }
    if (t->kind == TY_FLOAT || t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE) {
        cls[idx] = merge(cls[idx], ABI_SSE);
    } else {
        cls[idx] = merge(cls[idx], ABI_INTEGER);
    }
    /* A scalar spanning into the next eightbyte (only reachable through a
     * packed or misaligned aggregate) merges into both. */
    if (tl.size > 8 && idx + 1 < neightbytes && t->kind != TY_LDOUBLE)
        cls[idx + 1] = merge(cls[idx + 1], cls[idx]);
}

int layout_classify_sysv(Sema *s, Type *t, AbiClass out[2])
{
    TypeLayout tl;
    int n;

    out[0] = ABI_NO_CLASS;
    out[1] = ABI_NO_CLASS;
    if (!t || !layout_is_complete_for_size(t))
        return -1;
    tl = layout_of(s, t);
    /* Rule 2: anything larger than two eightbytes goes in MEMORY. The
     * psABI's SSE/SSEUP exception is only reachable through __m256 and
     * __m512, which are outside our surface. */
    if (tl.size > 16)
        return -1;
    n = tl.size > 8 ? 2 : 1;

    classify_into(s, t, 0, out, n);

    /* Post-merger cleanup, in the psABI's own order. */
    /* (a) any eightbyte MEMORY -> the whole argument is MEMORY. */
    if (out[0] == ABI_MEMORY || (n == 2 && out[1] == ABI_MEMORY))
        return -1;
    /* (b) an X87UP not preceded by X87 -> MEMORY. */
    if (n == 2 && out[1] == ABI_X87UP && out[0] != ABI_X87)
        return -1;
    /* An x87 long double inside an AGGREGATE ends in MEMORY via (a)/(b)
     * once merged; a BARE long double keeps X87/X87UP and is passed on
     * the stack — which is why this returns 2 rather than -1 for it. */
    /* (c) size > 16 already handled above. */
    /* (d) an SSEUP not preceded by SSE or SSEUP -> SSE. */
    if (n == 2 && out[1] == ABI_SSEUP && out[0] != ABI_SSE &&
        out[0] != ABI_SSEUP)
        out[1] = ABI_SSE;

    if (out[0] == ABI_NO_CLASS)
        out[0] = ABI_INTEGER;
    if (n == 2 && out[1] == ABI_NO_CLASS)
        out[1] = ABI_INTEGER;
    return n;
}

/* --- AAPCS64 homogeneous float aggregates -------------------------------- */

static bool hfa_walk(Sema *s, Type *t, Type **base, int *count)
{
    if (!t)
        return false;
    switch (t->kind) {
    case TY_FLOAT:
    case TY_DOUBLE:
    case TY_LDOUBLE:
        if (*base && (*base)->kind != t->kind)
            return false; /* not HOMOGENEOUS */
        *base = t;
        (*count)++;
        return *count <= 4;
    case TY_ARRAY: {
        u64 i;

        if (!t->has_size)
            return false;
        for (i = 0; i < t->size; i++)
            if (!hfa_walk(s, t->base, base, count))
                return false;
        return true;
    }
    case TY_STRUCT:
    case TY_UNION: {
        Member *m;

        if (!t->tag || !t->tag->complete || t->tag->nmembers == 0)
            return false;
        for (m = t->tag->members; m; m = m->next) {
            if (m->is_bitfield)
                return false;
            if (!hfa_walk(s, m->type, base, count))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

bool layout_is_hfa(Sema *s, Type *t, Type **base, int *count)
{
    Type *b = NULL;
    int n = 0;

    if (base)
        *base = NULL;
    if (count)
        *count = 0;
    /* An HFA is an aggregate — a bare float is not one. */
    if (!t ||
        (t->kind != TY_STRUCT && t->kind != TY_UNION && t->kind != TY_ARRAY))
        return false;
    if (!hfa_walk(s, t, &b, &n) || !b || n == 0 || n > 4)
        return false;
    if (base)
        *base = b;
    if (count)
        *count = n;
    return true;
}

/* --- the -fdump-layout debug dump ---------------------------------------- */

/* One line per record and per member: byte offset, bit offset and width
 * for bitfields, then the record's own size and alignment. Sprint 19
 * reuses this format rather than inventing a second one. Records print in
 * DECLARATION order — the repo-wide determinism law. */
static void dump_tag(Sema *s, TagDecl *tag, FILE *f)
{
    Member *m;
    Type ty;

    if (!tag || !tag->complete)
        return;
    memset(&ty, 0, sizeof(ty));
    ty.kind = tag->kind;
    ty.tag = tag;
    layout_record(s, &ty);

    fprintf(f, "%s %s: size=%llu align=%llu\n",
            tag->kind == TY_UNION ? "union" : "struct",
            tag->name ? tag->name : "<anonymous>",
            (unsigned long long)tag->size, (unsigned long long)tag->align);
    for (m = tag->members; m; m = m->next) {
        fprintf(f, "  %s: offset=%llu", m->name ? m->name : "<unnamed>",
                (unsigned long long)m->offset);
        if (m->is_bitfield)
            fprintf(f, " bit=%llu width=%u container=%llu",
                    (unsigned long long)m->bit_offset, m->bit_width,
                    (unsigned long long)m->container_size);
        else
            fprintf(f, " size=%llu align=%llu",
                    (unsigned long long)layout_of(s, m->type).size,
                    (unsigned long long)layout_of(s, m->type).align);
        fprintf(f, "\n");
    }
}

void layout_dump(Sema *s, FILE *f)
{
    Symbol *sym;
    Symbol *chain = s->file_scope ? s->file_scope->tags : NULL;
    Symbol **order;
    u32 n = 0, i;

    for (sym = chain; sym; sym = sym->next)
        n++;
    if (n == 0)
        return;
    /* The chain is newest-first; print in declaration order. */
    order = arena_alloc(s->arena, n * sizeof(Symbol *), _Alignof(Symbol *));
    i = n;
    for (sym = chain; sym; sym = sym->next)
        order[--i] = sym;
    for (i = 0; i < n; i++)
        if (order[i]->tag && order[i]->tag->kind != TY_ENUM)
            dump_tag(s, order[i]->tag, f);
}
