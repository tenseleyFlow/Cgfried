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
    case TY_FLOAT32:
        r.size = 4;
        r.align = 4;
        return r;
    case TY_DOUBLE:
    case TY_FLOAT64:
    case TY_FLOAT32X:
        r.size = 8;
        r.align = 8;
        return r;
    case TY_LDOUBLE:
    case TY_FLOAT64X:
        /* THE cross-target trap: 16/16 on x86-64 (x87 80-bit stored in 16
         * bytes) and on arm64-linux (IEEE binary128), but 8/8 on
         * arm64-macos where Apple makes long double the same as double. */
        r.size = tl.ldbl_size;
        r.align = tl.ldbl_align;
        return r;
    case TY_FLOAT128:
        /* NOT a target question, which is the whole point of the type:
         * _Float128 is IEEE binary128 everywhere, so it is 16/16 even on
         * x86-64 where `long double` is x87 80-bit. The two must never
         * share a row here or the distinction collapses. */
        r.size = 16;
        r.align = 16;
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
    bool has_zero_sized_member = false;

    for (m = tag->members; m; m = m->next) {
        TypeLayout ml;
        u64 malign;
        u64 natural_align;

        if (!m->type || !layout_is_complete_for_size(m->type)) {
            /* A flexible array member has no size and contributes none;
             * anything else incomplete was already diagnosed. */
            m->offset = align_up(offset_bits, 8) / 8;
            m->laid_out = true;
            continue;
        }
        ml = layout_of(s, m->type);
        natural_align = ml.align;
        malign = natural_align;
        if (!m->is_bitfield && ml.size == 0)
            has_zero_sized_member = true;
        /* `packed` drops the member's alignment to 1. The record's own
         * alignment then falls out of the same loop, because `align` only
         * ever rises to a member's requirement -- which is the half that is
         * easy to miss: force the offsets alone and the offsets are right
         * while sizeof keeps its tail padding. Measured against gcc in
         * .docs/audits/packed-layout.md. */
        if (m->packed)
            malign = 1;
        /* _Alignas on a member raises BOTH its own placement and the
         * record's alignment (6.7.5): the record must be aligned strictly
         * enough that every member lands where it asked to. */
        if (m->align_override > malign)
            malign = m->align_override;

        if (m->is_bitfield) {
            u64 unit_bits = declared_bits(s, m->type);
            u64 width = m->bit_width;

            m->container_size = ml.size;
            if (width == 0) {
                /* `T : 0` forces the NEXT field to the next boundary of
                 * T's alignment. SysV and Apple keep the usual rule that an
                 * unnamed field does not affect record alignment, but Linux
                 * AAPCS64 makes the zero-width field's base-type alignment a
                 * record requirement. SEMA-C-02: without this target split,
                 * `struct { long :0; int x; }` was 4/4 instead of 8/8 and
                 * disagreed with AAPCS64 callers and callees. */
                u64 barrier_bits = unit_bits;
                u64 zero_align = natural_align;

                if (m->align_override > zero_align)
                    zero_align = m->align_override;
                if (m->align_override * 8 > barrier_bits)
                    barrier_bits = m->align_override * 8;
                offset_bits = align_up(offset_bits, barrier_bits);
                /* AAPCS64 applies a zero-width field's BASE-TYPE alignment
                 * even when packed; SysV and Apple treat it only as an
                 * allocation barrier. GCC's target hook makes this split
                 * explicit, and the five-target table pins it. */
                if (s->target.kind == CGF_TARGET_ARM64_LINUX &&
                    zero_align > align)
                    align = zero_align;
                m->bit_offset = offset_bits;
                m->offset = offset_bits / 8;
                m->laid_out = true;
                continue;
            }
            /* An explicit GNU `aligned` still controls placement even when
             * `packed` reduced the implicit requirement to one byte. */
            if (m->align_override)
                offset_bits = align_up(offset_bits, m->align_override * 8);
            /* Rule 1: place at the current bit offset unless the field
             * would STRADDLE a boundary of its declared type — that is,
             * unless it fits in what remains of the current declared-type
             * -sized, declared-type-aligned window. This is the rule naive
             * implementations miss: in `struct { char a:7; int b:25; }`
             * the int window is bytes 0-3, bits 7..31 are free, and 25
             * fits — so b lands at bit 7, NOT at bit 32. */
            if (!m->packed) {
                u64 window_start = offset_bits / unit_bits * unit_bits;
                u64 used_in_window = offset_bits - window_start;

                if (used_in_window + width > unit_bits)
                    offset_bits = align_up(offset_bits, unit_bits);
            }
            m->bit_offset = offset_bits;
            m->offset = offset_bits / 8;
            offset_bits += width;
            /* SysV and Apple let only a named nonzero bitfield impose its
             * declared type's record alignment. Linux AAPCS64 also counts an
             * unnamed nonzero bitfield. SEMA-C-08: keeping the named-only
             * guard there produced ABI-incompatible struct alignment. */
            if ((m->name || s->target.kind == CGF_TARGET_ARM64_LINUX) &&
                malign > align)
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
    /* Preserve the compiler's nonzero recovery layout for a genuinely empty
     * record, while honoring GNU complete zero-sized members. In particular,
     * `struct { int x[0]; }` has size 0 and alignment 4, and that zero extent
     * must propagate through nesting and array stride. A flexible array is
     * incomplete and therefore never sets this flag. */
    if (tag->size == 0 && !has_zero_sized_member)
        tag->size = align;
}

static void layout_union(Sema *s, TagDecl *tag)
{
    Member *m;
    u64 size = 0;
    u64 align = 1;
    bool has_zero_sized_member = false;

    for (m = tag->members; m; m = m->next) {
        TypeLayout ml;
        u64 natural_align;

        m->offset = 0;
        m->bit_offset = 0;
        m->laid_out = true;
        if (!m->type || !layout_is_complete_for_size(m->type))
            continue;
        ml = layout_of(s, m->type);
        natural_align = ml.align;
        m->container_size = ml.size;
        if (!m->is_bitfield && ml.size == 0)
            has_zero_sized_member = true;
        /* A packed UNION keeps every member's SIZE -- they all start at 0, so
         * nothing can be misplaced -- and loses only its alignment. gcc:
         * `union { char a; double d; } packed` is 8 bytes, aligned 1. */
        if (m->packed)
            ml.align = 1;
        /* An _Alignas or `aligned` on a union MEMBER raises the union's own
         * alignment, exactly as it does in a struct. layout_union never read
         * align_override at all, so both spellings were silently ignored here
         * while working in a struct -- found by the layout differential the
         * first time it generated `aligned` on a union member. */
        if (m->align_override > ml.align)
            ml.align = m->align_override;
        if (m->is_bitfield) {
            /* Every union member starts at bit 0. A ZERO-WIDTH bitfield
             * occupies no storage at all. Linux AAPCS64 nevertheless makes
             * its base-type alignment a UNION requirement, just as it does
             * for a struct; Apple and SysV do not. SEMA-C-02's sibling hunt
             * caught this union half. It raises alignment only -- claiming a
             * whole container here was the older layout-differential bug. */
            /* Only the BITS are storage; the declared type governs
             * alignment and the straddle rule, not a mandatory whole
             * container. `union { unsigned long :7; int x; }` is 4 bytes,
             * not 8 — the second finding from the layout differential. */
            u64 bytes = (m->bit_width + 7) / 8;

            if (bytes > size)
                size = bytes;
            if (m->bit_width == 0 && s->target.kind == CGF_TARGET_ARM64_LINUX) {
                u64 zero_align = natural_align;

                if (m->align_override > zero_align)
                    zero_align = m->align_override;
                if (zero_align > align)
                    align = zero_align;
            }
            /* SEMA-C-08: Linux AAPCS64 applies the base-type alignment to an
             * unnamed nonzero union bitfield too; it still contributes only
             * its actual width to storage. */
            if ((m->name || (m->bit_width != 0 &&
                             s->target.kind == CGF_TARGET_ARM64_LINUX)) &&
                ml.align > align)
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
    if (tag->size == 0 && !has_zero_sized_member)
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
        break;
    }
    case TY_STRUCT:
    case TY_UNION:
        if (!t->tag || !t->tag->complete)
            break;
        else {
            layout_record(s, t);
            r.size = t->tag->size;
            r.align = t->tag->align;
            break;
        }
    case TY_ENUM:
        /* An enum has the size of its compatible integer type. */
        r = basic_layout(s, t->tag && t->tag->enum_underlying
                                ? t->tag->enum_underlying
                                : type_basic(TY_INT));
        break;
    default:
        r = basic_layout(s, t);
        break;
    }
    if (t->align_override > r.align)
        r.align = t->align_override;
    return r;
}

u64 layout_offsetof(Sema *s, Type *rec, const Member *m)
{
    layout_record(s, rec);
    return m ? m->offset : 0;
}

/* --- SysV x86-64 classification (psABI 3.2.3) ---------------------------- */

/* The pairwise merge, verbatim from the psABI. Order matters: MEMORY
 * poisons, INTEGER dominates anything that is not MEMORY, and x87 merged
 * with any distinct occupied class makes the aggregate MEMORY. */
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
         * what makes `union { double d; long l; }` come out INTEGER.
         *
         * A zero-width STRUCT bitfield is only an allocation barrier: it
         * occupies no bits and therefore contributes no ABI class. IR-C-01
         * used to merge its integer base type and poison a following sole
         * f80 into MEMORY. Preserve the UNION behavior deliberately: GCC 16
         * treats `union { int :0; long double v; }` as MEMORY, while Clang 22
         * returns it in st0; GCC is this lane's compatibility oracle until
         * that psABI ambiguity is resolved independently. */
        for (m = t->tag->members; m; m = m->next) {
            if (!m->type || !layout_is_complete_for_size(m->type))
                continue;
            if (t->kind == TY_STRUCT && m->is_bitfield && m->bit_width == 0)
                continue;
            classify_into(s, m->type, off + m->offset, cls, neightbytes);
        }
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

    if ((t->kind == TY_LDOUBLE || t->kind == TY_FLOAT64X) &&
        cgf_target_layout(s->target).ldbl_kind == CGF_LDBL_X87_80) {
        cls[idx] = merge(cls[idx], ABI_X87);
        if (idx + 1 < neightbytes)
            cls[idx + 1] = merge(cls[idx + 1], ABI_X87UP);
        return;
    }
    if (t->kind == TY_FLOAT128 ||
        (t->kind == TY_FLOAT64X &&
         cgf_target_layout(s->target).ldbl_kind == CGF_LDBL_IEEE128)) {
        /* psABI: __float128 is SSE in its first eightbyte and SSEUP in its
         * second, so it travels in ONE xmm register rather than two.
         * Measured: gcc compiles `__float128 add(__float128 a, __float128 b)`
         * to a bare `call __addtf3`, forwarding xmm0/xmm1 untouched. */
        cls[idx] = merge(cls[idx], ABI_SSE);
        if (idx + 1 < neightbytes)
            cls[idx + 1] = merge(cls[idx + 1], ABI_SSEUP);
        return;
    }
    if (t->kind == TY_FLOAT || t->kind == TY_DOUBLE || t->kind == TY_LDOUBLE ||
        t->kind == TY_FLOAT32 || t->kind == TY_FLOAT64 ||
        t->kind == TY_FLOAT32X || t->kind == TY_FLOAT64X) {
        cls[idx] = merge(cls[idx], ABI_SSE);
    } else {
        cls[idx] = merge(cls[idx], ABI_INTEGER);
    }
    /* A scalar spanning into the next eightbyte (only reachable through a
     * packed or misaligned aggregate) merges into both. */
    if (tl.size > 8 && idx + 1 < neightbytes && t->kind != TY_LDOUBLE)
        cls[idx + 1] = merge(cls[idx + 1], cls[idx]);
}

/* psABI 3.2.3 rule 1, the clause packed makes reachable: an aggregate that
 * "contains unaligned fields" is MEMORY regardless of size. Verified against
 * gcc, including the NEGATIVE -- `struct { int b; } packed` has alignment 1
 * yet every field sits at its natural offset, and gcc passes it in a register.
 * So the test is the OFFSET, not the record's alignment.
 *
 * `off` accumulates through nesting, which is what catches a packed struct
 * embedded at an odd offset in an ordinary one. */
static bool has_unaligned_field(Sema *s, Type *t, u64 off)
{
    if (!t || !layout_is_complete_for_size(t))
        return false;
    switch (t->kind) {
    case TY_STRUCT:
    case TY_UNION: {
        Member *m;

        if (!t->tag || !t->tag->complete)
            return false;
        layout_record(s, t);
        for (m = t->tag->members; m; m = m->next) {
            u64 moff;

            if (!m->type || !layout_is_complete_for_size(m->type))
                continue;
            moff = off + m->offset;
            /* A bitfield's byte offset says nothing about alignment; its
             * container is the record's business, not the classifier's. */
            if (!m->is_bitfield) {
                TypeLayout ml = layout_of(s, m->type);

                if (ml.align && moff % ml.align != 0)
                    return true;
            }
            if (has_unaligned_field(s, m->type, moff))
                return true;
        }
        return false;
    }
    case TY_ARRAY: {
        TypeLayout el = layout_of(s, t->base);

        /* Elements are naturally spaced, so checking the FIRST checks all:
         * if element 0 is aligned every later one is, and if it is not,
         * element 0 already proved the aggregate unaligned. */
        if (el.align && off % el.align != 0)
            return true;
        return t->size ? has_unaligned_field(s, t->base, off) : false;
    }
    default:
        return false;
    }
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
    if (has_unaligned_field(s, t, 0))
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
    /* IR-C-01: classification is direction-neutral. A bare f80 and an
     * exactly-16-byte aggregate containing only that f80 both remain
     * X87/X87UP here. SysV arguments demote that pair to memory, while
     * returns use st0; erasing it here forced aggregate returns through an
     * ABI-incompatible hidden pointer. Any additional occupied class already
     * became MEMORY through merge(), and sizes above 16 were rejected. */
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
    case TY_FLOAT128:
    case TY_FLOAT32:
    case TY_FLOAT64:
    case TY_FLOAT32X:
    case TY_FLOAT64X:
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
    case TY_STRUCT: {
        Member *m;

        if (!t->tag || !t->tag->complete || t->tag->nmembers == 0)
            return false;
        for (m = t->tag->members; m; m = m->next) {
            if (m->is_bitfield)
                return false;
            /* An over-aligned member introduces padding beyond the natural
             * layout, so the aggregate is no longer a plain sequence of
             * base-type values and AAPCS64 stops treating it as an HFA. */
            if (m->align_override)
                return false;
            if (!hfa_walk(s, m->type, base, count))
                return false;
        }
        return true;
    }
    case TY_UNION: {
        Member *m;
        int best;

        if (!t->tag || !t->tag->complete || t->tag->nmembers == 0)
            return false;
        /* Union members OVERLAY, so the leaves this union contributes is
         * the MAX over its members, never the sum. Summing rejected
         * `union { float f[3]; struct { float x, y, z; } v; }` as a
         * six-leaf aggregate; clang --target=aarch64-linux-gnu passes it
         * in s0-s2, i.e. three leaves. Sprint 14 shipped no union row at
         * all, which is how the sum survived to Sprint 48. */
        best = *count;
        for (m = t->tag->members; m; m = m->next) {
            int sub = *count; /* every member starts from the same base */

            if (m->is_bitfield || m->align_override)
                return false;
            if (!hfa_walk(s, m->type, base, &sub))
                return false;
            if (sub > best)
                best = sub;
        }
        *count = best;
        return *count <= 4;
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
