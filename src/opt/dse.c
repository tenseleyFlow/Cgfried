#include "opt/opt.h"

#include <limits.h>
#include <string.h>

#include "opt/alias.h"
#include "util/arena.h"

typedef struct {
    bool active;
    bool object_known;
    u32 object;
    i64 lo;
    i64 hi; /* half-open */
    MemLoc loc;
} Cover;

static u64 type_size(IrType type)
{
    switch (type) {
    case IRT_I8:
        return 1;
    case IRT_I16:
        return 2;
    case IRT_I32:
    case IRT_F32:
        return 4;
    case IRT_I64:
    case IRT_F64:
    case IRT_PTR:
        return 8;
    case IRT_F128:
    case IRT_V16I8:
    case IRT_V8I16:
    case IRT_V4I32:
    case IRT_V2I64:
    case IRT_V4F32:
    case IRT_V2F64:
        return 16;
    case IRT_F80:
        /* The IR has no TargetSpec: x87 object size and payload width differ.
         * Stay conservative instead of inventing host-derived byte coverage;
         * the scan must treat a load of this unknown width as a read barrier.
         */
        return 0;
    default:
        return 0;
    }
}

static bool singleton_object(AliasCtx *alias, IrOperand ptr, u32 *object)
{
    PtsSet pts = alias_points_to(alias, ptr);
    u32 found = UINT32_MAX;
    u32 wi;

    if (pts.has_unknown)
        return false;
    for (wi = 0; wi < pts.nwords; wi++) {
        u64 bits = pts.words[wi];

        while (bits) {
            u32 bit = 0;
            u64 scan = bits;

            while ((scan & 1) == 0) {
                bit++;
                scan >>= 1;
            }
            if (found != UINT32_MAX)
                return false;
            found = wi * 64 + bit;
            bits &= bits - 1;
        }
    }
    if (found == UINT32_MAX)
        return false;
    *object = found;
    return true;
}

static bool known_interval(AliasCtx *alias, IrOperand ptr, u64 size, i64 *lo,
                           i64 *hi)
{
    i64 base_lo, base_hi;

    if (!size || size > (u64)INT64_MAX ||
        !alias_offset_range(alias, ptr, &base_lo, &base_hi) ||
        base_lo != base_hi || base_lo > INT64_MAX - (i64)size)
        return false;
    *lo = base_lo;
    *hi = base_lo + (i64)size;
    return true;
}

static bool write_loc(AliasCtx *alias, const IrInst *in, MemLoc *loc,
                      bool *object_known, u32 *object, i64 *lo, i64 *hi)
{
    IrOperand ptr;
    u64 size;

    if (in->op == IR_STORE && in->nops == 2) {
        ptr = in->ops[1];
        size = type_size((IrType)in->ops[0].type);
    } else if (in->op == IR_MEMSET && in->nops == 3 &&
               in->ops[2].kind == IROP_ICONST) {
        ptr = in->ops[0];
        size = in->ops[2].a;
    } else {
        return false;
    }
    if (!size)
        return false;
    *loc = alias_memloc(alias, ptr, size, (EffTypeId)in->subop);
    *object_known = singleton_object(alias, ptr, object) &&
                    known_interval(alias, ptr, size, lo, hi);
    return true;
}

static bool read_loc(AliasCtx *alias, const IrInst *in, MemLoc *loc)
{
    u64 size;

    if (in->op != IR_LOAD || in->nops != 1)
        return false;
    size = type_size((IrType)in->type);
    if (!size)
        return false;
    *loc = alias_memloc(alias, in->ops[0], size, (EffTypeId)in->subop);
    return true;
}

static bool is_barrier(const IrInst *in)
{
    if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
        return true;
    switch ((IrOp)in->op) {
    case IR_CALL:
    case IR_MEMCPY:
    case IR_VA_START:
    case IR_STACKSAVE:
    case IR_STACKRESTORE:
    case IR_ATOMICRMW:
    case IR_CMPXCHG:
    /* A `"memory"` clobber means exactly this barrier, and an asm without
     * one still writes its outputs through addresses this pass cannot
     * follow. Treating every asm as a barrier is the conservative reading
     * and the only one that is right without parsing the template. */
    case IR_ASM:
        return true;
    default:
        return false;
    }
}

static void clear_covers(Cover *covers, u32 ncovers)
{
    u32 i;

    for (i = 0; i < ncovers; i++)
        covers[i].active = false;
}

static void apply_read(AliasCtx *alias, Cover *covers, u32 ncovers, MemLoc read)
{
    u32 i;

    for (i = 0; i < ncovers; i++)
        if (covers[i].active &&
            alias_query(alias, read, covers[i].loc) != ALIAS_NO)
            covers[i].active = false;
}

static bool aggregate_covers(const Cover *covers, u32 ncovers, u32 object,
                             i64 lo, i64 hi)
{
    i64 at = lo;

    while (at < hi) {
        i64 reach = at;
        u32 i;

        for (i = 0; i < ncovers; i++)
            if (covers[i].active && covers[i].object_known &&
                covers[i].object == object && covers[i].lo <= at &&
                covers[i].hi > reach)
                reach = covers[i].hi;
        if (reach == at)
            return false;
        at = reach;
    }
    return true;
}

static bool aggregate_overlaps(const Cover *covers, u32 ncovers, u32 object,
                               i64 lo, i64 hi)
{
    u32 i;

    for (i = 0; i < ncovers; i++)
        if (covers[i].active && covers[i].object_known &&
            covers[i].object == object && covers[i].lo < hi &&
            lo < covers[i].hi)
            return true;
    return false;
}

static bool any_single_cover(AliasCtx *alias, const Cover *covers, u32 ncovers,
                             MemLoc loc)
{
    u32 i;

    for (i = 0; i < ncovers; i++)
        if (covers[i].active && alias_covers(alias, covers[i].loc, loc))
            return true;
    return false;
}

static void add_cover(Cover *covers, u32 *ncovers, MemLoc loc,
                      bool object_known, u32 object, i64 lo, i64 hi)
{
    Cover *out = &covers[(*ncovers)++];

    out->active = true;
    out->object_known = object_known;
    out->object = object;
    out->lo = lo;
    out->hi = hi;
    out->loc = loc;
}

static void seed_dead_allocas(AliasCtx *alias, const IrFunc *f, Cover *covers,
                              u32 *ncovers)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            IrOperand ptr;
            MemLoc loc;
            u32 object;
            i64 lo, hi;
            u64 size;

            if (in->op != IR_ALLOCA || !in->result.v || in->nops != 1 ||
                in->ops[0].kind != IROP_ICONST)
                continue;
            size = in->ops[0].a;
            ptr = ir_op_value(f, in->result);
            if (!size || alias_escapes(alias, ptr) ||
                !singleton_object(alias, ptr, &object) ||
                !known_interval(alias, ptr, size, &lo, &hi))
                continue;
            loc = alias_memloc(alias, ptr, size, (EffTypeId)in->subop);
            add_cover(covers, ncovers, loc, true, object, lo, hi);
        }
    }
}

static bool dse_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    OptConfig fc = *cfg;
    AliasConfig acfg = {
        .func = f,
        .no_strict_aliasing = cfg->no_strict_aliasing,
    };
    AliasCtx *alias = alias_build(m, &acfg);
    u32 total = 0;
    bool changed = false;
    bool call_bailed = false;
    bool unknown_load_bailed = false;
    bool partial_bailed = false;
    u32 bi;
    bool *remove;
    IrInst **insts;
    u32 *ids;
    Cover *covers;
    u32 serial = 0;

    fc.current_func = f->name;
    for (bi = 0; bi < f->nblocks; bi++)
        total += f->blocks[bi].ninsts;
    arena_init(&scratch);
    remove = arena_alloc(&scratch, (total ? total : 1) * sizeof(*remove), 1);
    memset(remove, 0, (total ? total : 1) * sizeof(*remove));
    insts = arena_alloc(&scratch, (total ? total : 1) * sizeof(*insts),
                        _Alignof(IrInst *));
    ids = arena_alloc(&scratch, (total ? total : 1) * sizeof(*ids),
                      _Alignof(u32));
    covers =
        arena_alloc(&scratch, (total + 1) * sizeof(*covers), _Alignof(Cover));

    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *blk = &f->blocks[bi];
        u32 ninsts = 0, ncovers = 0;
        IrInst *in;
        bool exit_block;
        u32 i;

        memset(covers, 0, (total + 1) * sizeof(*covers));
        for (in = blk->first; in; in = in->next) {
            insts[ninsts] = in;
            ids[ninsts++] = serial++;
        }
        exit_block = blk->last && (blk->last->op == IR_RET ||
                                   blk->last->op == IR_UNREACHABLE);
        if (exit_block)
            seed_dead_allocas(alias, f, covers, &ncovers);

        for (i = ninsts; i-- > 0;) {
            MemLoc loc;
            bool object_known;
            u32 object = 0;
            i64 lo = 0, hi = 0;

            in = insts[i];
            if (is_barrier(in)) {
                if (in->op == IR_CALL && !call_bailed) {
                    OPT_BAIL(&fc, "dse", "dse_call_barrier");
                    call_bailed = true;
                }
                clear_covers(covers, ncovers);
                continue;
            }
            if (read_loc(alias, in, &loc)) {
                apply_read(alias, covers, ncovers, loc);
                continue;
            }
            if (in->op == IR_LOAD) {
                /* A load whose target-dependent width is unknown still reads
                 * memory. Ignoring it let the synthetic end-of-function
                 * cover erase union member stores that feed an f80 load --
                 * exactly musl's ldshape idiom. */
                if (!unknown_load_bailed) {
                    OPT_BAIL(&fc, "dse", "dse_unknown_load_size");
                    unknown_load_bailed = true;
                }
                clear_covers(covers, ncovers);
                continue;
            }
            if (!write_loc(alias, in, &loc, &object_known, &object, &lo, &hi))
                continue;
            if (any_single_cover(alias, covers, ncovers, loc) ||
                (object_known &&
                 aggregate_covers(covers, ncovers, object, lo, hi))) {
                remove[ids[i]] = true;
                changed = true;
                continue;
            }
            if (object_known &&
                aggregate_overlaps(covers, ncovers, object, lo, hi) &&
                !partial_bailed) {
                OPT_BAIL(&fc, "dse", "dse_partial_overwrite");
                partial_bailed = true;
            }
            add_cover(covers, &ncovers, loc, object_known, object, lo, hi);
        }
    }

    /* AliasCtx is invalid after the first unlink.  All proofs above are
     * therefore completed before this mutation phase begins. */
    alias_free(alias);
    serial = 0;
    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *blk = &f->blocks[bi];
        IrInst *in = blk->first;
        IrInst *prev = NULL;

        while (in) {
            IrInst *next = in->next;

            if (remove[serial++]) {
                if (prev)
                    prev->next = next;
                else
                    blk->first = next;
                if (blk->last == in)
                    blk->last = prev;
                blk->ninsts--;
            } else {
                prev = in;
            }
            in = next;
        }
    }
    if (changed)
        ir_func_renumber(m->arena, f);
    arena_free_all(&scratch);
    return changed;
}

bool opt_dse(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= dse_func(m, &m->funcs[i], cfg);
    return changed;
}

const Pass OPT_PASS_DSE = {"dse", opt_dse, PASS_PINNED_EXACT};
