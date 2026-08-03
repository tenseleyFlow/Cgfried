#include "lower/lower.h"

#include <stdio.h>
#include <string.h>

/* Emission-side initializers (Sprint 19): the string-literal pool and
 * the tiered local-aggregate strategies.
 *
 * THE THRESHOLDS — one table, retuned (if ever) by Sprint 53:
 *   <= STORE_MAX bytes, fully constant -> inline scalar stores (no
 *      template, no memset; padding bytes get written too, which C
 *      permits for locals);
 *   >  STORE_MAX, fully constant       -> memcpy from a deduped .rodata
 *      template — except a trailing zero run > ZERO_TAIL_MIN splits into
 *      head-init + one memset, and an ALL-zero image is exactly one
 *      memset and no template at all ({0} on a big array);
 *   any runtime-valued elements        -> the constant part lands FIRST
 *      (by the strategies above, runtime slots zeroed), then each
 *      runtime element stores AFTER, in source order. Copy-then-store,
 *      never store-then-copy: the template would clobber the runtime
 *      values. Element SIDE EFFECTS stay in source order relative to
 *      each other (the Sprint 18 §1 law); the constant stores carry no
 *      side effects and may precede them all. */
#define LOWER_INIT_STORE_MAX 32
#define LOWER_INIT_ZERO_TAIL_MIN 16

/* --- the string/template pool ---------------------------------------------
 *
 * Content-keyed dedup: identical BYTES (including the NUL and, for
 * strings, the encoding class) share one symbol. Emission order is
 * first-occurrence order — the pool map's iteration order never reaches
 * output (globals append at creation), which is the determinism law.
 * Literals are const; there is no writable-strings compat flag. */

static u32 pool_intern(Lower *lo, u8 tag, const u8 *bytes, u64 n,
                       const char *prefix, u32 align)
{
    /* Key = one tag byte + the content. */
    char small[256];
    char *key = n + 1 <= sizeof(small)
                    ? small
                    : arena_alloc(lo->arena, (size_t)n + 1, 1);
    void *hit;
    char buf[32];
    IrGlobal *g;
    u32 idx;

    key[0] = (char)tag;
    if (n)
        memcpy(key + 1, bytes, (size_t)n);
    hit = strmap_get(&lo->string_pool, key, (size_t)n + 1);
    if (hit)
        return (u32)(uintptr_t)hit - 1;
    snprintf(buf, sizeof(buf), "%s%u", prefix, lo->nstrings++);
    g = ir_global_new(lo->m, arena_strdup(lo->arena, buf));
    g->size = n;
    g->align = align;
    g->linkage = IRLINK_INTERNAL;
    g->init = arena_alloc(lo->arena, n ? (size_t)n : 1, 1);
    if (n)
        memcpy(g->init, bytes, (size_t)n);
    idx = ir_sym(lo->m, g->name);
    strmap_put(&lo->string_pool, key, (size_t)n + 1,
               (void *)(uintptr_t)(idx + 1));
    return idx;
}

u32 lower_string_lit(Lower *lo, const AstNode *e)
{
    u64 n = e->tok ? e->tok->str.nbytes : 0;
    u8 *bytes = arena_alloc(lo->arena, (size_t)n + 1, 1);

    if (n && e->tok)
        memcpy(bytes, e->tok->str.bytes, (size_t)n);
    bytes[n] = 0; /* the terminator the source spelling implies */
    /* The encoding class joins the key: L"a" and "a" may share bytes on
     * some targets but are different literals. */
    return pool_intern(lo, (u8)(0x10 + (e->tok ? e->tok->str.enc : 0)), bytes,
                       n + 1, ".Lstr.", 1);
}

/* --- constant-image builder ------------------------------------------------
 *
 * A quiet mirror of constexpr.c's fill(): constants pack into the image
 * (CE_FOLD never diagnoses), runtime elements append to the residue
 * list in SOURCE order. */

typedef struct RtStore {
    i64 off;
    Type *t;          /* scalar or aggregate element type */
    AstNode *e;       /* the expression to evaluate at runtime */
    const Member *bf; /* bitfield member, or NULL */
    struct RtStore *next;
} RtStore;

typedef struct InitPlan {
    Lower *lo;
    u8 *img;
    u64 size;
    RtStore *rt_head, *rt_tail;
    /* relocs found in the constant part (address-constant elements) */
    struct {
        u64 off;
        Symbol *sym;
        const AstNode *anon;
        i64 addend;
    } relocs[32];
    u32 nrelocs;
    bool reloc_overflow;
} InitPlan;

static void plan_rt(InitPlan *p, i64 off, Type *t, AstNode *e, const Member *bf)
{
    RtStore *r = arena_alloc(p->lo->arena, sizeof(RtStore), _Alignof(RtStore));

    r->off = off;
    r->t = t;
    r->e = e;
    r->bf = bf;
    r->next = NULL;
    if (p->rt_tail)
        p->rt_tail->next = r;
    else
        p->rt_head = r;
    p->rt_tail = r;
}

static void plan_put_int(InitPlan *p, u64 off, u64 v, u64 width)
{
    u64 i;

    for (i = 0; i < width && off + i < p->size; i++)
        p->img[off + i] = (u8)(v >> (i * 8));
}

static void plan_scalar(InitPlan *p, Type *t, AstNode *e, i64 off)
{
    Sema *s = p->lo->sema;
    ConstValue v;
    TypeLayout l;

    if (!e)
        return;
    l = layout_of(s, t);
    v = constexpr_eval(s, e, CE_FOLD); /* silent on failure by design */
    switch (v.kind) {
    case CV_INT:
        plan_put_int(p, (u64)off, v.i, l.size);
        return;
    case CV_FLOAT: {
        uint8_t b[16];
        u64 i;

        sf_to_bits(v.f, constexpr_format_of(s, t), b);
        for (i = 0; i < l.size && (u64)off + i < p->size; i++)
            p->img[off + i] = b[i];
        return;
    }
    case CV_ADDR:
        if (p->nrelocs < 32) {
            p->relocs[p->nrelocs].off = (u64)off;
            p->relocs[p->nrelocs].sym = v.sym;
            p->relocs[p->nrelocs].anon = v.anon;
            p->relocs[p->nrelocs].addend = v.addend;
            p->nrelocs++;
        } else {
            p->reloc_overflow = true;
            plan_rt(p, off, t, e, NULL);
        }
        return;
    default:
        plan_rt(p, off, t, e, NULL);
        return;
    }
}

static void plan_walk(InitPlan *p, Type *t, AstNode *init, i64 off);

#define PLAN_CURSOR_MAX 128

typedef struct PlanCursorFrame {
    Type *aggregate;
    u64 off;
    u64 pos;
} PlanCursorFrame;

typedef struct PlanCursor {
    PlanCursorFrame frames[PLAN_CURSOR_MAX];
    u32 depth;
    Type *current;
    Member *member;
    u64 off;
} PlanCursor;

static bool plan_is_aggregate(const Type *t)
{
    return t &&
           (t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_UNION);
}

static bool plan_cursor_select(InitPlan *p, PlanCursor *cursor)
{
    PlanCursorFrame *frame;
    Member *member;
    u64 at = 0;

    cursor->current = NULL;
    cursor->member = NULL;
    if (!cursor->depth)
        return false;
    frame = &cursor->frames[cursor->depth - 1];
    if (frame->aggregate->kind == TY_ARRAY) {
        TypeLayout element;

        if (!frame->aggregate->base || (frame->aggregate->has_size &&
                                        frame->pos >= frame->aggregate->size))
            return false;
        element = layout_of(p->lo->sema, frame->aggregate->base);
        cursor->current = frame->aggregate->base;
        cursor->off = frame->off + frame->pos * element.size;
        return true;
    }
    if ((frame->aggregate->kind != TY_STRUCT &&
         frame->aggregate->kind != TY_UNION) ||
        !frame->aggregate->tag)
        return false;
    layout_record(p->lo->sema, frame->aggregate);
    for (member = frame->aggregate->tag->members; member;
         member = member->next) {
        if (member->is_bitfield && !member->name)
            continue;
        if (at++ != frame->pos)
            continue;
        cursor->current = member->type;
        cursor->member = member;
        cursor->off =
            member->is_bitfield ? frame->off : frame->off + member->offset;
        return true;
    }
    return false;
}

static void plan_cursor_start(InitPlan *p, PlanCursor *cursor, Type *root,
                              u64 off)
{
    memset(cursor, 0, sizeof(*cursor));
    cursor->frames[0].aggregate = root;
    cursor->frames[0].off = off;
    cursor->depth = 1;
    (void)plan_cursor_select(p, cursor);
}

static bool plan_cursor_descend(InitPlan *p, PlanCursor *cursor)
{
    Type *aggregate = cursor->current;
    u64 off = cursor->off;

    if (!plan_is_aggregate(aggregate) || cursor->depth >= PLAN_CURSOR_MAX)
        return false;
    cursor->frames[cursor->depth].aggregate = aggregate;
    cursor->frames[cursor->depth].off = off;
    cursor->frames[cursor->depth].pos = 0;
    cursor->depth++;
    return plan_cursor_select(p, cursor);
}

static void plan_cursor_advance(InitPlan *p, PlanCursor *cursor)
{
    while (cursor->depth) {
        PlanCursorFrame *frame = &cursor->frames[cursor->depth - 1];

        if (frame->aggregate->kind != TY_UNION) {
            frame->pos++;
            if (plan_cursor_select(p, cursor))
                return;
        }
        cursor->depth--;
    }
    cursor->current = NULL;
    cursor->member = NULL;
}

static bool plan_member_path(Type *aggregate, const char *name, u64 *path,
                             u32 capacity, u32 *depth)
{
    Member *member;
    u64 at = 0;

    if (!aggregate || !aggregate->tag || !name || !capacity)
        return false;
    for (member = aggregate->tag->members; member; member = member->next) {
        if (member->is_bitfield && !member->name)
            continue;
        if (member->name == name) {
            path[0] = at;
            *depth = 1;
            return true;
        }
        if (!member->name && member->type && capacity > 1 &&
            (member->type->kind == TY_STRUCT ||
             member->type->kind == TY_UNION)) {
            u32 inner_depth = 0;

            if (plan_member_path(member->type, name, path + 1, capacity - 1,
                                 &inner_depth)) {
                path[0] = at;
                *depth = inner_depth + 1;
                return true;
            }
        }
        at++;
    }
    return false;
}

static bool plan_cursor_designate(InitPlan *p, PlanCursor *cursor, Type *root,
                                  u64 off, const AstNode *item)
{
    u32 i;

    plan_cursor_start(p, cursor, root, off);
    for (i = 0; item && i < item->ndesignators; i++) {
        const AstNode *designator = item->designators[i];
        PlanCursorFrame *frame;
        u64 pos;

        if (!designator || !cursor->depth)
            return false;
        frame = &cursor->frames[cursor->depth - 1];
        if (designator->desig_is_field) {
            u64 path[PLAN_CURSOR_MAX];
            u32 path_depth = 0;
            u32 path_index;

            if ((frame->aggregate->kind != TY_STRUCT &&
                 frame->aggregate->kind != TY_UNION) ||
                !plan_member_path(frame->aggregate, designator->desig_field,
                                  path, PLAN_CURSOR_MAX - cursor->depth + 1,
                                  &path_depth))
                return false;
            for (path_index = 0; path_index < path_depth; path_index++) {
                frame = &cursor->frames[cursor->depth - 1];
                frame->pos = path[path_index];
                if (!plan_cursor_select(p, cursor))
                    return false;
                if (path_index + 1 < path_depth) {
                    if (!plan_is_aggregate(cursor->current) ||
                        cursor->depth >= PLAN_CURSOR_MAX)
                        return false;
                    cursor->frames[cursor->depth].aggregate = cursor->current;
                    cursor->frames[cursor->depth].off = cursor->off;
                    cursor->frames[cursor->depth].pos = 0;
                    cursor->depth++;
                }
            }
        } else {
            ConstValue index;

            if (frame->aggregate->kind != TY_ARRAY || !designator->desig_index)
                return false;
            index =
                constexpr_eval(p->lo->sema, designator->desig_index, CE_FOLD);
            if (index.kind != CV_INT || (i64)index.i < 0)
                return false;
            pos = index.i;
            frame->pos = pos;
            if (!plan_cursor_select(p, cursor))
                return false;
        }
        if (i + 1 < item->ndesignators) {
            if (!plan_is_aggregate(cursor->current) ||
                cursor->depth >= PLAN_CURSOR_MAX)
                return false;
            cursor->frames[cursor->depth].aggregate = cursor->current;
            cursor->frames[cursor->depth].off = cursor->off;
            cursor->frames[cursor->depth].pos = 0;
            cursor->depth++;
        }
    }
    return true;
}

static bool plan_expr_initializes_whole(Type *target, const AstNode *init)
{
    if (!target || !init)
        return false;
    if (target->kind == TY_ARRAY && init->kind == AST_EXPR_STRING)
        return true;
    return init->sem_type && type_compatible(target, init->sem_type);
}

static void plan_cursor_value(InitPlan *p, const PlanCursor *cursor,
                              AstNode *item)
{
    Member *member = cursor->member;

    if (!cursor->current)
        return;
    if (member && member->is_bitfield) {
        ConstValue value = constexpr_eval(p->lo->sema, item, CE_FOLD);

        if (value.kind == CV_INT) {
            u32 bit;

            for (bit = 0; bit < member->bit_width; bit++) {
                u64 absolute = cursor->off * 8 + member->bit_offset + bit;
                u64 byte = absolute / 8;

                if (byte >= p->size)
                    break;
                if ((value.i >> bit) & 1)
                    p->img[byte] |= (u8)(1u << (absolute % 8));
            }
        } else {
            plan_rt(p, (i64)cursor->off, member->type, item, member);
        }
        return;
    }
    plan_walk(p, cursor->current, item, (i64)cursor->off);
}

static void plan_aggregate_list(InitPlan *p, Type *t, AstNode *init, u64 off)
{
    PlanCursor cursor;
    u32 k;

    plan_cursor_start(p, &cursor, t, off);
    for (k = 0; k < init->nitems; k++) {
        AstNode *item = init->items[k];

        if (!item)
            continue;
        if (item->ndesignators &&
            !plan_cursor_designate(p, &cursor, t, off, item))
            continue;
        if (!cursor.current)
            continue;
        if (item->kind == AST_INIT_LIST) {
            plan_cursor_value(p, &cursor, item);
            plan_cursor_advance(p, &cursor);
            continue;
        }
        while (plan_is_aggregate(cursor.current) &&
               !plan_expr_initializes_whole(cursor.current, item))
            if (!plan_cursor_descend(p, &cursor))
                break;
        plan_cursor_value(p, &cursor, item);
        plan_cursor_advance(p, &cursor);
    }
}

static void plan_array(InitPlan *p, Type *t, AstNode *init, i64 off)
{
    if (init->kind == AST_EXPR_STRING) {
        u64 cap = t->has_size ? t->size : 0;
        u64 n = init->tok ? init->tok->str.nbytes : 0;
        u64 i;

        if (n > cap)
            n = cap;
        for (i = 0; i < n && (u64)off + i < p->size; i++)
            p->img[off + i] = init->tok->str.bytes[i];
        return;
    }
    if (init->kind != AST_INIT_LIST) {
        plan_walk(p, t->base, init, off);
        return;
    }
    plan_aggregate_list(p, t, init, (u64)off);
}

static void plan_record(InitPlan *p, Type *t, AstNode *init, i64 off)
{
    if (!t->tag)
        return;
    layout_record(p->lo->sema, t);
    if (init->kind != AST_INIT_LIST) {
        plan_rt(p, off, t, init, NULL); /* runtime whole-struct memcpy */
        return;
    }
    plan_aggregate_list(p, t, init, (u64)off);
}

static void plan_walk(InitPlan *p, Type *t, AstNode *init, i64 off)
{
    if (!t || !init)
        return;
    switch (t->kind) {
    case TY_ARRAY:
        plan_array(p, t, init, off);
        return;
    case TY_STRUCT:
    case TY_UNION:
        plan_record(p, t, init, off);
        return;
    default:
        if (init->kind == AST_INIT_LIST) {
            if (init->nitems > 0)
                plan_scalar(p, t, init->items[0], off);
            return;
        }
        plan_scalar(p, t, init, off);
        return;
    }
}

/* --- emission helpers ------------------------------------------------------
 */

static IrOperand off_addr(Lower *lo, IrOperand base, i64 off)
{
    ValueId p;

    if (off == 0)
        return base;
    p = ir_build_ptradd(&lo->b, base, lower_i64(off));
    return ir_op_value(lo->fn, p);
}

static void store_chunk(Lower *lo, IrOperand base, i64 off, IrType t, u64 bits,
                        u32 align)
{
    Lvalue lv;

    memset(&lv, 0, sizeof(lv));
    lv.addr = off_addr(lo, base, off);
    lv.unit = t;
    lv.etype = ETYPE_CHAR; /* constant image bytes, not a typed C access */
    lv.align = align;
    lower_store(lo, lv, ir_op_iconst(t, (i64)bits));
}

static u64 read_le(const u8 *b, u32 n)
{
    u64 v = 0;
    u32 i;

    for (i = 0; i < n; i++)
        v |= (u64)b[i] << (i * 8);
    return v;
}

/* Inline scalar stores covering [0, size): 8-byte chunks then 4/2/1.
 * Reloc slots interleave as ptr stores of the symbol. */
static void emit_stores(Lower *lo, InitPlan *p, IrOperand base, u32 align)
{
    u64 off = 0;
    u32 r = 0;

    while (off < p->size) {
        u64 next_reloc = r < p->nrelocs ? p->relocs[r].off : p->size;

        if (off == next_reloc) {
            IrOperand sv;
            Lvalue lv;
            u32 sym = p->relocs[r].sym ? lower_global_sym(lo, p->relocs[r].sym)
                                       : lower_anon_sym(lo, p->relocs[r].anon);

            sv = ir_op_symbol(IRT_PTR, sym, p->relocs[r].addend);
            memset(&lv, 0, sizeof(lv));
            lv.addr = off_addr(lo, base, (i64)off);
            lv.unit = IRT_PTR;
            lv.etype = ETYPE_PTR;
            lv.align = align < 8 ? align : 8;
            lower_store(lo, lv, sv);
            off += 8;
            r++;
            continue;
        }
        {
            u64 room = next_reloc - off;
            u32 chunk = room >= 8 ? 8 : room >= 4 ? 4 : room >= 2 ? 2 : 1;
            IrType t = chunk == 8   ? IRT_I64
                       : chunk == 4 ? IRT_I32
                       : chunk == 2 ? IRT_I16
                                    : IRT_I8;

            store_chunk(lo, base, (i64)off, t, read_le(p->img + off, chunk),
                        align < chunk ? align : chunk);
            off += chunk;
        }
    }
}

/* The constant part of an image, by the threshold table. `has_rt` only
 * affects nothing here — runtime slots are zero in the image. */
static void emit_const_part(Lower *lo, InitPlan *p, IrOperand base, u32 align)
{
    u64 z = 0; /* trailing zero run */
    bool all_zero;

    while (z < p->size && p->img[p->size - 1 - z] == 0)
        z++;
    /* A reloc in the tail keeps the tail (pointers are not zeroes even
     * when their image bytes are). */
    if (p->nrelocs) {
        u64 last_reloc_end = p->relocs[p->nrelocs - 1].off + 8;

        if (p->size - z < last_reloc_end)
            z = p->size - last_reloc_end;
    }
    all_zero = z == p->size && p->nrelocs == 0;

    if (all_zero) {
        /* {0} and friends: exactly one memset, any size. */
        ir_build_memset(&lo->b, base, ir_op_iconst(IRT_I32, 0),
                        lower_i64((i64)p->size), align, 0);
        return;
    }
    if (p->size <= LOWER_INIT_STORE_MAX) {
        emit_stores(lo, p, base, align);
        return;
    }
    if (z > LOWER_INIT_ZERO_TAIL_MIN) {
        /* Zero-tail split: init the head, one memset for the tail. */
        u64 head = p->size - z;
        InitPlan hp = *p;

        hp.size = head;
        if (head <= LOWER_INIT_STORE_MAX) {
            emit_stores(lo, &hp, base, align);
        } else {
            u32 tmpl = pool_intern(lo, 0x01, p->img, head, ".Lconst.", align);

            ir_build_memcpy(&lo->b, base, ir_op_symbol(IRT_PTR, tmpl, 0),
                            lower_i64((i64)head), align, 0);
        }
        ir_build_memset(&lo->b, off_addr(lo, base, (i64)head),
                        ir_op_iconst(IRT_I32, 0), lower_i64((i64)z), 1, 0);
        return;
    }
    /* Full-size template. With relocs the template is unique (its bytes
     * alone are not its identity), so it bypasses the dedup pool. */
    if (p->nrelocs) {
        char buf[32];
        IrGlobal *g;
        u32 i, idx;

        snprintf(buf, sizeof(buf), ".Lconst.%u", lo->nstrings++);
        g = ir_global_new(lo->m, arena_strdup(lo->arena, buf));
        g->size = p->size;
        g->align = align;
        g->linkage = IRLINK_INTERNAL;
        g->init = p->img;
        g->relocs = arena_alloc(lo->arena, p->nrelocs * sizeof(IrReloc),
                                _Alignof(IrReloc));
        g->nrelocs = p->nrelocs;
        for (i = 0; i < p->nrelocs; i++) {
            g->relocs[i].offset = p->relocs[i].off;
            g->relocs[i].addend = p->relocs[i].addend;
            g->relocs[i].symbol = p->relocs[i].sym
                                      ? lower_global_sym(lo, p->relocs[i].sym)
                                      : lower_anon_sym(lo, p->relocs[i].anon);
        }
        idx = ir_sym(lo->m, g->name);
        ir_build_memcpy(&lo->b, base, ir_op_symbol(IRT_PTR, idx, 0),
                        lower_i64((i64)p->size), align, 0);
        return;
    }
    {
        u32 tmpl = pool_intern(lo, 0x01, p->img, p->size, ".Lconst.", align);

        ir_build_memcpy(&lo->b, base, ir_op_symbol(IRT_PTR, tmpl, 0),
                        lower_i64((i64)p->size), align, 0);
    }
}

/* One runtime element, after the constant part landed. */
static void emit_rt_store(Lower *lo, IrOperand base, RtStore *r)
{
    if (r->bf) {
        const Member *m = r->bf;
        u64 cbits = m->container_size * 8;
        u64 unit_byte = (m->bit_offset / cbits) * m->container_size;
        IrOperand v = lower_rvalue(lo, r->e);
        Lvalue lv;

        v = lower_scalar_convert(lo, v, r->e->sem_type, (Type *)m->type);
        memset(&lv, 0, sizeof(lv));
        lv.addr = off_addr(lo, base, r->off + (i64)unit_byte);
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
        lv.etype = lower_efftype(lo, m->type);
        lv.is_bitfield = true;
        lv.bit_shift = (u8)(m->bit_offset - unit_byte * 8);
        lv.bit_width = (u8)m->bit_width;
        lv.is_signed = conv_is_signed(lo->sema, (Type *)m->type);
        lower_store(lo, lv, v);
        return;
    }
    if (lower_is_aggregate(r->t)) {
        IrOperand src = lower_rvalue(lo, r->e);
        TypeLayout l = layout_of(lo->sema, r->t);

        lower_memcpy_aggregate(lo, off_addr(lo, base, r->off), src, r->t,
                               (u32)l.align, 0);
        return;
    }
    {
        IrOperand v = lower_rvalue(lo, r->e);
        TypeLayout l = layout_of(lo->sema, r->t);
        Lvalue lv;

        v = lower_scalar_convert(lo, v, r->e->sem_type, r->t);
        memset(&lv, 0, sizeof(lv));
        lv.addr = off_addr(lo, base, r->off);
        lv.unit = lower_irtype(lo, r->t);
        lv.etype = lower_efftype(lo, r->t);
        lv.align = (u32)(l.align ? l.align : 1);
        lower_store(lo, lv, v);
    }
}

void lower_local_init(Lower *lo, IrOperand base, Type *t, AstNode *init)
{
    TypeLayout l;

    if (!init)
        return;
    /* Scalars and whole-object copies keep their direct paths. */
    if (!lower_is_aggregate(t)) {
        InitPlan dummy;
        AstNode *e =
            init->kind == AST_INIT_LIST && init->nitems ? init->items[0] : init;
        IrOperand v;
        Lvalue lv;

        (void)dummy;
        if (init->kind == AST_INIT_LIST && !init->nitems)
            return;
        v = lower_rvalue(lo, e);
        v = lower_scalar_convert(lo, v, e->sem_type, t);
        memset(&lv, 0, sizeof(lv));
        lv.addr = base;
        lv.unit = lower_irtype(lo, t);
        lv.etype = lower_efftype(lo, t);
        l = layout_of(lo->sema, t);
        lv.align = (u32)(l.align ? l.align : 1);
        if (t->quals & CGF_QUAL_VOLATILE)
            lv.is_volatile = true;
        lower_store(lo, lv, v);
        return;
    }
    l = layout_of(lo->sema, t);
    if (init->kind != AST_INIT_LIST && init->kind != AST_EXPR_STRING) {
        /* struct x = expr: one memcpy (the Sprint 18 §8 law). */
        IrOperand src = lower_rvalue(lo, init);

        lower_memcpy_aggregate(lo, base, src, t, (u32)l.align, 0);
        return;
    }
    {
        InitPlan p;
        RtStore *r;

        memset(&p, 0, sizeof(p));
        p.lo = lo;
        p.size = l.size;
        p.img = arena_alloc(lo->arena, l.size ? (size_t)l.size : 1, 8);
        memset(p.img, 0, l.size ? (size_t)l.size : 1);
        plan_walk(&p, t, init, 0);
        emit_const_part(lo, &p, base, (u32)(l.align ? l.align : 1));
        for (r = p.rt_head; r; r = r->next)
            emit_rt_store(lo, base, r);
    }
}
