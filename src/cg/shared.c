#include "cg/shared.h"

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "ir/ir.h"
#include "util/sort.h"

void cg_emit_elf_idents(const IrModule *m, Buf *out)
{
    u32 i, j;

    if (!m->nidents)
        return;
    /* ELF .comment conventionally begins with a NUL. GNU as's `.ident`
     * creates that byte once, then appends each decoded string and its NUL. */
    buf_printf(out, "\t.section\t.comment,\"\",@progbits\n");
    buf_printf(out, "\t.byte\t0\n");
    for (i = 0; i < m->nidents; i++) {
        for (j = 0; j < m->idents[i].len; j++)
            buf_printf(out, "\t.byte\t%u\n", m->idents[i].bytes[j]);
        buf_printf(out, "\t.byte\t0\n");
    }
}

static void bit_set(u64 *words, u32 bit)
{
    words[bit >> 6] |= 1ull << (bit & 63);
}

static void bit_clear(u64 *words, u32 bit)
{
    words[bit >> 6] &= ~(1ull << (bit & 63));
}

static bool bit_get(const u64 *words, u32 bit)
{
    return (words[bit >> 6] >> (bit & 63)) & 1;
}

u32 cg_liveness_words(const CgMirView *view)
{
    return (view->nvregs + 64) / 64;
}

void cg_liveness(const CgMirView *view, u64 *live_in, u64 *live_out)
{
    u32 words = cg_liveness_words(view);
    u64 *tmp = cgf_xmalloc(words * sizeof(u64));
    u32 *uses =
        cgf_xmalloc((view->max_uses ? view->max_uses : 1) * sizeof(u32));
    bool changed = true;
    u32 bi, w;

    while (changed) {
        changed = false;
        for (bi = view->nblocks; bi-- > 0;) {
            u64 *in = &live_in[bi * words];
            u64 *out = &live_out[bi * words];
            i64 ii;

            memset(tmp, 0, words * sizeof(u64));
            for (ii = (i64)view->block_ninsts(view->ctx, bi) - 1; ii >= 0;
                 ii--) {
                const u32 *targets;
                u32 nt = view->inst_targets(view->ctx, bi, (u32)ii, &targets);
                u32 def, nu, k;

                for (k = 0; k < nt; k++) {
                    const u64 *target_in;

                    if (!targets[k] || targets[k] > view->nblocks)
                        CGF_ICE("liveness: invalid block target %u",
                                targets[k]);
                    target_in = &live_in[(targets[k] - 1) * words];
                    for (w = 0; w < words; w++) {
                        tmp[w] |= target_in[w];
                        out[w] |= target_in[w];
                    }
                }
                def = view->inst_def(view->ctx, bi, (u32)ii);
                if (def > view->nvregs)
                    CGF_ICE("liveness: invalid vreg %u", def);
                if (def)
                    bit_clear(tmp, def);
                nu = view->inst_uses(view->ctx, bi, (u32)ii, uses,
                                     view->max_uses);
                if (nu > view->max_uses)
                    CGF_ICE("liveness: MIR view use capacity exceeded");
                for (k = 0; k < nu; k++) {
                    if (uses[k] > view->nvregs)
                        CGF_ICE("liveness: invalid vreg %u", uses[k]);
                    if (uses[k])
                        bit_set(tmp, uses[k]);
                }
            }
            for (w = 0; w < words; w++)
                if (tmp[w] != in[w]) {
                    in[w] = tmp[w];
                    changed = true;
                }
        }
    }
    free(uses);
    free(tmp);
}

static void interval_extend(CgInterval *intervals, u32 vreg, u32 point)
{
    CgInterval *it = &intervals[vreg];

    if (!it->live) {
        it->live = true;
        it->vreg = vreg;
        it->start = it->end = point;
    } else {
        if (point < it->start)
            it->start = point;
        if (point > it->end)
            it->end = point;
    }
}

static void mark_call_crossings(const CgMirView *view, const u64 *live_in,
                                CgInterval *intervals)
{
    u32 words = cg_liveness_words(view);
    u64 *tmp = cgf_xmalloc(words * sizeof(u64));
    u32 *uses =
        cgf_xmalloc((view->max_uses ? view->max_uses : 1) * sizeof(u32));
    u32 bi, w;

    if (!view->inst_clobbers_call) {
        free(uses);
        free(tmp);
        return;
    }
    for (bi = view->nblocks; bi-- > 0;) {
        i64 ii;

        memset(tmp, 0, words * sizeof(u64));
        for (ii = (i64)view->block_ninsts(view->ctx, bi) - 1; ii >= 0; ii--) {
            const u32 *targets;
            u32 nt = view->inst_targets(view->ctx, bi, (u32)ii, &targets);
            u32 def, nu, k, v;

            for (k = 0; k < nt; k++) {
                const u64 *target_in;

                if (!targets[k] || targets[k] > view->nblocks)
                    CGF_ICE("call crossings: invalid block target %u",
                            targets[k]);
                target_in = &live_in[(targets[k] - 1) * words];
                for (w = 0; w < words; w++)
                    tmp[w] |= target_in[w];
            }
            def = view->inst_def(view->ctx, bi, (u32)ii);
            if (def > view->nvregs)
                CGF_ICE("call crossings: invalid vreg %u", def);
            if (view->inst_clobbers_call(view->ctx, bi, (u32)ii)) {
                /* tmp is the exact live-after set. A call result may already
                 * be in it, but the value does not exist until this call
                 * defines it and therefore does not cross this clobber. */
                if (def)
                    bit_clear(tmp, def);
                for (v = 1; v <= view->nvregs; v++)
                    if (bit_get(tmp, v))
                        intervals[v].live_across_call = true;
            }
            if (def)
                bit_clear(tmp, def);
            nu = view->inst_uses(view->ctx, bi, (u32)ii, uses, view->max_uses);
            if (nu > view->max_uses)
                CGF_ICE("call crossings: MIR view use capacity exceeded");
            for (k = 0; k < nu; k++) {
                if (uses[k] > view->nvregs)
                    CGF_ICE("call crossings: invalid vreg %u", uses[k]);
                if (uses[k])
                    bit_set(tmp, uses[k]);
            }
        }
    }
    free(uses);
    free(tmp);
}

void cg_intervals_build(const CgMirView *view, u64 *live_in, u64 *live_out,
                        CgInterval *intervals)
{
    u32 words = cg_liveness_words(view);
    u32 *uses =
        cgf_xmalloc((view->max_uses ? view->max_uses : 1) * sizeof(u32));
    u32 point = 0, bi, i, v;

    memset(intervals, 0, (view->nvregs + 1) * sizeof(*intervals));
    cg_liveness(view, live_in, live_out);
    for (bi = 0; bi < view->nblocks; bi++) {
        u32 ninsts = view->block_ninsts(view->ctx, bi);
        u32 start = point;
        u32 end = point + (ninsts ? ninsts - 1 : 0);

        for (v = 1; v <= view->nvregs; v++) {
            if (bit_get(&live_in[bi * words], v))
                interval_extend(intervals, v, start);
            if (bit_get(&live_out[bi * words], v))
                interval_extend(intervals, v, end);
        }
        for (i = 0; i < ninsts; i++) {
            u32 def = view->inst_def(view->ctx, bi, i);
            u32 nu, k;

            if (def > view->nvregs)
                CGF_ICE("intervals: invalid vreg %u", def);
            if (def)
                interval_extend(intervals, def, start + i);
            nu = view->inst_uses(view->ctx, bi, i, uses, view->max_uses);
            if (nu > view->max_uses)
                CGF_ICE("intervals: MIR view use capacity exceeded");
            for (k = 0; k < nu; k++) {
                if (uses[k] > view->nvregs)
                    CGF_ICE("intervals: invalid vreg %u", uses[k]);
                if (uses[k])
                    interval_extend(intervals, uses[k], start + i);
            }
        }
        point = end + 1;
    }
    mark_call_crossings(view, live_in, intervals);
    free(uses);
}

i32 cg_spill_slot_assign(CgSpillSlots *slots, u32 size, u32 align)
{
    u32 raw;

    if (!size || (size & (size - 1)) || !align || (align & (align - 1)))
        CGF_ICE("spill slot size/alignment must be powers of two");
    raw = (u32)(-slots->next_offset) + size;
    raw = (raw + align - 1) & ~(align - 1);
    slots->next_offset = -(i32)raw;
    slots->count++;
    return slots->next_offset;
}

static int interval_start_cmp(const void *pa, const void *pb, void *ctx)
{
    const CgInterval *a = *(CgInterval *const *)pa;
    const CgInterval *b = *(CgInterval *const *)pb;

    (void)ctx;
    if (a->start != b->start)
        return a->start < b->start ? -1 : 1;
    return a->vreg < b->vreg ? -1 : a->vreg > b->vreg ? 1 : 0;
}

static void spill(CgInterval *it, const CgLinearScanPolicy *policy,
                  CgSpillSlots *slots)
{
    if (!it->slot)
        it->slot = cg_spill_slot_assign(
            slots, policy->spill_size(policy->ctx, it->vreg),
            policy->spill_align(policy->ctx, it->vreg));
}

void cg_linear_scan(CgInterval *intervals, u32 nvregs,
                    const CgLinearScanPolicy *policy, CgSpillSlots *slots)
{
    CgInterval **order = cgf_xmalloc((nvregs + 1) * sizeof(*order));
    CgInterval **active = cgf_xmalloc((nvregs + 1) * sizeof(*active));
    bool *used = cgf_xmalloc((policy->nphys_regs ? policy->nphys_regs : 1) *
                             sizeof(*used));
    u32 nactive = 0, norder = 0;
    u32 v, i, k;

    for (v = 1; v <= nvregs; v++)
        if (intervals[v].live)
            order[norder++] = &intervals[v];
    cgf_sort_stable(order, norder, sizeof(*order), interval_start_cmp, NULL);

    for (i = 0; i < norder; i++) {
        CgInterval *cur = order[i];
        const u8 *pool = NULL;
        u32 npool = 0;

        for (k = 0; k < nactive;) {
            if (active[k]->end < cur->start)
                active[k] = active[--nactive];
            else
                k++;
        }
        if (cur->fixed) {
            if (cur->fixed > policy->nphys_regs)
                CGF_ICE("linear scan: invalid fixed physical register %u",
                        cur->fixed - 1);
            cur->phys = cur->fixed;
            active[nactive++] = cur;
            continue;
        }
        /* `spill_all` is a stress lane, not a semantics change: an interval
         * that CANNOT be spilled still allocates normally under it. */
        if (policy->spill_all && !cur->no_spill) {
            spill(cur, policy, slots);
            continue;
        }

        policy->pool(policy->ctx, cur->vreg, &pool, &npool);
        memset(used, 0, policy->nphys_regs * sizeof(*used));
        for (k = 0; k < nactive; k++) {
            if (active[k]->phys > policy->nphys_regs)
                CGF_ICE("linear scan: invalid active physical register %u",
                        active[k]->phys - 1);
            if (active[k]->phys)
                used[active[k]->phys - 1] = true;
        }
        cur->phys = 0;
        for (k = 0; k < npool; k++) {
            u8 reg = pool[k];

            if (reg >= policy->nphys_regs)
                CGF_ICE("linear scan: invalid physical register %u", reg);
            if (!used[reg] && policy->reg_usable(policy->ctx, cur->vreg, reg,
                                                 cur->start, cur->end)) {
                cur->phys = (u8)(reg + 1);
                break;
            }
        }
        if (!cur->phys) {
            CgInterval *far = NULL;

            for (k = 0; k < nactive; k++) {
                CgInterval *cand = active[k];
                u8 reg;

                if (cand->fixed || cand->no_spill || !cand->phys ||
                    !policy->same_class(policy->ctx, cand->vreg, cur->vreg))
                    continue;
                reg = (u8)(cand->phys - 1);
                if (!policy->reg_usable(policy->ctx, cur->vreg, reg, cur->start,
                                        cur->end))
                    continue;
                if (!far || cand->end > far->end)
                    far = cand;
            }
            /* The usual heuristic spills whichever of the two lives longer.
             * An unspillable interval overrides it: someone else goes,
             * whatever the lengths. */
            if (far && (far->end > cur->end || cur->no_spill)) {
                cur->phys = far->phys;
                far->phys = 0;
                spill(far, policy, slots);
                for (k = 0; k < nactive; k++)
                    if (active[k] == far) {
                        active[k] = cur;
                        break;
                    }
            } else if (cur->no_spill) {
                /* Nothing evictable and no free register: the asm asked for
                 * more registers at once than the target has. gcc reports
                 * this as "'asm' operand has impossible constraints"; we
                 * cannot, because the allocator holds no diagnostic context
                 * and no span. Reaching it needs more simultaneously-live
                 * register operands than the machine has allocatable
                 * registers, which lower_asm caps against -- so this is the
                 * backstop for that cap, not the reporting site. */
                CGF_ICE("linear scan: v%u must be in a register and none is "
                        "available (inline asm register pressure)",
                        cur->vreg);
            } else {
                spill(cur, policy, slots);
            }
            continue;
        }
        active[nactive++] = cur;
    }
    free(used);
    free(active);
    free(order);
}

/* Alignment is a byte count in the source and an EXPONENT in the directive, so
 * the conversion happens exactly once, here. A FUNCTION request below the
 * backend's own default is declined, and a non-power-of-two cannot arrive,
 * sema having rejected it. */
unsigned cg_func_p2align(const struct IrFunc *irf, unsigned dflt)
{
    unsigned p2 = 0;
    u32 want;

    if (!irf || !irf->align)
        return dflt;
    want = irf->align;
    while ((1u << p2) < want && p2 < 31)
        p2++;
    return p2 > dflt ? p2 : dflt;
}
