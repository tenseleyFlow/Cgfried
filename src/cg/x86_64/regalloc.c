#include "cg/x86_64/mir.h"

#include <stdlib.h>
#include <string.h>

#include "driver/toolchain.h"
#include "util/sort.h"

/* Sprint 22: backward liveness, the SIMPLE linear scan (one interval per
 * vreg, [first_point, last_point], NO holes — hole-aware allocation buys
 * a few % for real interval-splitting complexity; revisit with Sprint 53
 * benchmarks), pre-colored intervals for the fixed-reg constraints isel
 * recorded, spill code, the two-address fixup with the dst==src2 hazard
 * table, and the rbp frame under the 16-byte alignment law.
 *
 * ONE code path at every opt level. There is deliberately no opt-level
 * anything in this file (a CI grep enforces it): a -O0-only allocator
 * is a bug farm.
 *
 * Register discipline:
 *   allocatable GP (12): rax rcx rdx rsi rdi r8 r9 | rbx r12 r13 r14 r15
 *     — caller-saved first (free in leaf ranges), callee-saved after
 *     (push/pop cost paid once per function).
 *   reserved: rsp rbp (frame), r11 (a-side reload + spilled-def scratch)
 *     and r10 (b-side reload + two-address rescue + dynamic alloca temp).
 *     Two reserved scratches because one instruction can need a reload
 *     for each of two spilled sources.
 *   SysV callee-saved: rbx rbp r12-r15 ONLY. No xmm is callee-saved, so
 *     FP values living across calls always spill — Sprint 23 inherits
 *     that for free from this machinery.
 *
 * Pre-colored constraints: isel already materializes the copies (the
 * div rax/rdx dance IS movs onto dedicated vregs), so constrained vregs
 * are simply force-colored. When two same-color fixed intervals overlap
 * anyway (two live CL counts; a quotient living across a second div),
 * the conflict is REPAIRED by localizing every fixed site of the
 * offending vregs through fresh tiny vregs, then liveness reruns. */

#define SCRATCH_A X64_R11
#define SCRATCH_B X64_R10
/* xmm mirrors the GP scratch discipline (Sprint 23): xmm15 = a-side
 * reload + spilled-def home, xmm14 = b-side reload. 14 allocatable. */
#define SCRATCH_XA X64_XMM15
#define SCRATCH_XB X64_XMM14

static const u8 alloc_order[] = {
    X64_RAX, X64_RCX, X64_RDX, X64_RSI, X64_RDI, X64_R8,
    X64_R9,  X64_RBX, X64_R12, X64_R13, X64_R14, X64_R15,
};
#define NALLOC ((u32)(sizeof(alloc_order) / sizeof(alloc_order[0])))

/* Arg registers first (free coalescing-by-luck at call sites), the rest
 * after. ALL xmm are caller-saved, so a call-crossing xmm interval never
 * gets a register at all — it spills (the SysV surprise). */
static const u8 xmm_order[] = {
    X64_XMM0, X64_XMM1, X64_XMM2, X64_XMM3,  X64_XMM4,  X64_XMM5,  X64_XMM6,
    X64_XMM7, X64_XMM8, X64_XMM9, X64_XMM10, X64_XMM11, X64_XMM12, X64_XMM13,
};
#define NXMM ((u32)(sizeof(xmm_order) / sizeof(xmm_order[0])))

static bool is_callee_saved(u8 reg)
{
    return reg == X64_RBX || reg == X64_R12 || reg == X64_R13 ||
           reg == X64_R14 || reg == X64_R15;
}

typedef struct Interval {
    u32 vreg;
    u32 start, end; /* inclusive instruction points */
    bool live;
    u8 fixed; /* X64Reg + 1: pre-colored (unspillable) */
    u8 phys;  /* X64Reg + 1 once assigned */
    i32 slot; /* spill slot rbp-disp; 0 = not spilled */
} Interval;

typedef struct Ra {
    X64Func *f;
    Arena *arena;
    Interval *iv; /* [nvregs+1], vreg-indexed */
    u64 *live_in; /* [nblocks * words] */
    u64 *live_out;
    u32 words;
    i32 next_slot; /* grows downward; slots at -8, -16, ... */
    u32 *call_pts; /* sorted global points of CALL instructions */
    u32 ncalls;
} Ra;

/* An interval STRICTLY spanning a call keeps its value live across the
 * clobber: caller-saved is off the table. Defs at the call (results) and
 * uses ending at it (arguments) do not span. */
static bool crosses_call(const Ra *ra, u32 start, u32 end)
{
    u32 i;

    for (i = 0; i < ra->ncalls; i++)
        if (start < ra->call_pts[i] && ra->call_pts[i] < end)
            return true;
    return false;
}

/* --- small vector for rebuilding instruction streams ----------------------
 *
 * Every pass that inserts instructions REBUILDS the block: in-place
 * memmove insertion while walking is exactly how an inserted spill store
 * gets re-visited as if it were user code. The map (old index -> new
 * index) is what keeps flags_src honest across insertions. */

typedef struct Rb {
    Arena *arena;
    X64Inst *v;
    u32 n, cap;
    u32 *map; /* [old_n] old inst index -> new index */
} Rb;

static void rb_init(Rb *rb, Arena *a, u32 old_n)
{
    memset(rb, 0, sizeof(*rb));
    rb->arena = a;
    rb->map = arena_alloc(a, (old_n ? old_n : 1) * sizeof(u32), 4);
}

static void rb_put(Rb *rb, const X64Inst *in)
{
    if (rb->n == rb->cap) {
        u32 nc = rb->cap ? rb->cap * 2 : 16;
        X64Inst *nv =
            arena_alloc(rb->arena, nc * sizeof(X64Inst), _Alignof(X64Inst));

        if (rb->n)
            memcpy(nv, rb->v, rb->n * sizeof(X64Inst));
        rb->v = nv;
        rb->cap = nc;
    }
    rb->v[rb->n++] = *in;
}

/* Install the rebuilt stream and remap every flags_src through map. */
static void rb_commit(Rb *rb, X64Block *b)
{
    u32 i;

    for (i = 0; i < rb->n; i++)
        if (rb->v[i].flags & X64IF_USES_FLAGS)
            rb->v[i].flags_src = rb->map[rb->v[i].flags_src];
    b->insts = rb->v;
    b->n = rb->n;
    b->cap = rb->cap;
}

/* --- operand walking -------------------------------------------------------
 */

/* A call can carry 6 GP + 8 xmm arg uses plus AL on top of its operands,
 * so the walk buffer is sized for the worst case. */
#define MAX_USES 24

static u32 inst_uses(const X64Inst *in, X64VReg out[MAX_USES])
{
    u32 n = 0, k;

    if (in->a.kind == X64O_VREG && in->a.r.v)
        out[n++] = in->a.r;
    if (in->b.kind == X64O_VREG && in->b.r.v)
        out[n++] = in->b.r;
    if (in->a.kind == X64O_MEM) {
        if (in->a.mem.base.v)
            out[n++] = in->a.mem.base;
        if (in->a.mem.index.v)
            out[n++] = in->a.mem.index;
    }
    if (in->b.kind == X64O_MEM) {
        if (in->b.mem.base.v)
            out[n++] = in->b.mem.base;
        if (in->b.mem.index.v)
            out[n++] = in->b.mem.index;
    }
    for (k = 0; k < in->nxuses && n < MAX_USES; k++)
        if (in->xuses[k].r.v)
            out[n++] = in->xuses[k].r;
    return n;
}

static bool inst_is_branch(const X64Inst *in)
{
    return in->op == X64_OP_JMP || in->op == X64_OP_JCC ||
           in->op == X64_OP_JMPTBL;
}

/* Branch targets of one instruction (jmptbl fans out via its table;
 * jcc's target/target2 are adjacent fields, so one pointer serves). */
static u32 inst_targets(const X64Func *f, const X64Inst *in, const u32 **tab)
{
    if (in->op == X64_OP_JMPTBL) {
        *tab = f->tables[in->table].targets;
        return f->tables[in->table].n;
    }
    *tab = &in->target;
    if (in->op == X64_OP_JCC && in->target2)
        return 2;
    return in->target ? 1 : 0;
}

/* --- liveness --------------------------------------------------------------
 */

static void bit_set(u64 *w, u32 i)
{
    w[i >> 6] |= 1ull << (i & 63);
}

static void bit_clear(u64 *w, u32 i)
{
    w[i >> 6] &= ~(1ull << (i & 63));
}

static bool bit_get(const u64 *w, u32 i)
{
    return (w[i >> 6] >> (i & 63)) & 1;
}

u32 x64_liveness_words(const X64Func *f)
{
    return (f->nvregs + 64) / 64;
}

/* Backward dataflow to fixpoint. A block may branch MID-stream (switch
 * compare trees put several jccs in one block), so the transfer walks
 * instructions and unions each branch's target live-in AT the branch —
 * killing a def that sits below a jcc must not hide a value that
 * escaped through it. live_out is the union of every target's live-in
 * (a conservative summary; interval building consumes it). */
void x64_liveness(const X64Func *f, u64 *live_in, u64 *live_out)
{
    u32 words = x64_liveness_words(f);
    u64 *tmp = cgf_xmalloc(words * 8);
    bool changed = true;
    u32 bi, w;

    while (changed) {
        changed = false;
        for (bi = f->nblocks; bi-- > 0;) {
            const X64Block *b = &f->blocks[bi];
            u64 *in_ = &live_in[bi * words];
            u64 *out_ = &live_out[bi * words];
            i64 ii;

            memset(tmp, 0, words * 8);
            for (ii = (i64)b->n - 1; ii >= 0; ii--) {
                const X64Inst *x = &b->insts[ii];
                X64VReg uses[MAX_USES];
                u32 nu, k;

                if (inst_is_branch(x)) {
                    const u32 *tgt;
                    u32 nt = inst_targets(f, x, &tgt);

                    for (k = 0; k < nt; k++) {
                        const u64 *tin = &live_in[(tgt[k] - 1) * words];

                        for (w = 0; w < words; w++) {
                            tmp[w] |= tin[w];
                            out_[w] |= tin[w];
                        }
                    }
                }
                if (x->def.v)
                    bit_clear(tmp, x->def.v);
                nu = inst_uses(x, uses);
                for (k = 0; k < nu; k++)
                    bit_set(tmp, uses[k].v);
            }
            for (w = 0; w < words; w++)
                if (tmp[w] != in_[w]) {
                    in_[w] = tmp[w];
                    changed = true;
                }
        }
    }
    free(tmp);
}

/* --- intervals -------------------------------------------------------------
 */

static void iv_extend(Ra *ra, u32 v, u32 p)
{
    Interval *it = &ra->iv[v];

    if (!it->live) {
        it->live = true;
        it->vreg = v;
        it->start = it->end = p;
        return;
    }
    if (p < it->start)
        it->start = p;
    if (p > it->end)
        it->end = p;
}

static void build_intervals(Ra *ra)
{
    X64Func *f = ra->f;
    u32 p = 0, bi, i, v;

    ra->words = x64_liveness_words(f);
    ra->live_in = arena_alloc(ra->arena, f->nblocks * ra->words * 8, 8);
    ra->live_out = arena_alloc(ra->arena, f->nblocks * ra->words * 8, 8);
    memset(ra->live_in, 0, f->nblocks * ra->words * 8);
    memset(ra->live_out, 0, f->nblocks * ra->words * 8);
    ra->ncalls = 0; /* rebuilt every repair iteration */
    x64_liveness(f, ra->live_in, ra->live_out);

    ra->iv = arena_alloc(ra->arena, (f->nvregs + 1) * sizeof(Interval),
                         _Alignof(Interval));
    memset(ra->iv, 0, (f->nvregs + 1) * sizeof(Interval));
    for (bi = 0; bi < f->nblocks; bi++) {
        const X64Block *b = &f->blocks[bi];
        u32 bstart = p;
        u32 bend = p + (b->n ? b->n - 1 : 0);

        for (v = 1; v <= f->nvregs; v++) {
            if (bit_get(&ra->live_in[bi * ra->words], v))
                iv_extend(ra, v, bstart);
            if (bit_get(&ra->live_out[bi * ra->words], v))
                iv_extend(ra, v, bend);
        }
        for (i = 0; i < b->n; i++) {
            const X64Inst *in = &b->insts[i];
            X64VReg uses[MAX_USES];
            u32 nu, k;

            if (in->op == X64_OP_CALL) {
                if (!ra->call_pts)
                    ra->call_pts =
                        arena_alloc(ra->arena, 1024 * sizeof(u32), 4);
                if (ra->ncalls < 1024)
                    ra->call_pts[ra->ncalls++] = bstart + i;
                else
                    CGF_ICE("regalloc: >1024 call sites in one function");
            }
            if (in->def.v)
                iv_extend(ra, in->def.v, bstart + i);
            nu = inst_uses(in, uses);
            for (k = 0; k < nu; k++)
                iv_extend(ra, uses[k].v, bstart + i);
        }
        p = bend + 1;
    }
}

/* Fixed colors come from isel's annotations. A vreg carrying two
 * DIFFERENT colors is an isel bug — ICE, not repair. */
static void collect_fixed(Ra *ra)
{
    const X64Func *f = ra->f;
    u32 bi, i, k;

    for (bi = 0; bi < f->nblocks; bi++) {
        const X64Block *b = &f->blocks[bi];

        for (i = 0; i < b->n; i++) {
            const X64Inst *in = &b->insts[i];
            struct {
                u32 v;
                u8 c;
            } site[4];
            u32 ns = 0;

            if (in->a.kind == X64O_VREG && in->a.fixed) {
                site[ns].v = in->a.r.v;
                site[ns++].c = in->a.fixed;
            }
            if (in->b.kind == X64O_VREG && in->b.fixed) {
                site[ns].v = in->b.r.v;
                site[ns++].c = in->b.fixed;
            }
            if (in->def.v && in->def_fixed) {
                site[ns].v = in->def.v;
                site[ns++].c = in->def_fixed;
            }
            for (k = 0; k < ns; k++) {
                Interval *it = &ra->iv[site[k].v];

                if (it->fixed && it->fixed != site[k].c)
                    CGF_ICE("regalloc: v%u constrained to two registers",
                            site[k].v);
                it->fixed = site[k].c;
            }
            for (k = 0; k < in->nxuses; k++) {
                Interval *it;

                if (!in->xuses[k].r.v || !in->xuses[k].fixed)
                    continue;
                it = &ra->iv[in->xuses[k].r.v];
                if (it->fixed && it->fixed != in->xuses[k].fixed)
                    CGF_ICE("regalloc: v%u constrained to two registers",
                            in->xuses[k].r.v);
                it->fixed = in->xuses[k].fixed;
            }
        }
    }
}

/* --- pre-color conflict repair ---------------------------------------------
 *
 * Localize every fixed site of `v` through a fresh tiny vreg bridged by
 * a copy, leaving v itself unconstrained. Fixed use: `mov t, v` just
 * before, operand retargeted to t. Fixed def: the inst defines t, then
 * `mov v, t` just after. Copies are full-width — safe for any value. */

static X64Inst mk_mov(X64VReg def, X64VReg src)
{
    X64Inst mv;

    memset(&mv, 0, sizeof(mv));
    mv.op = X64_OP_MOV;
    mv.width = X64_Q;
    mv.def = def;
    mv.a.kind = X64O_VREG;
    mv.a.r = src;
    return mv;
}

/* Class-aware register-register copy: xmm values move with FMOV (movsd);
 * a GP mov of an xmm vreg is not a thing. */
static X64Inst mk_copy(const X64Func *f, X64VReg def, X64VReg src, u8 rc)
{
    X64Inst mv = mk_mov(def, src);

    (void)f;
    if (rc == X64RC_XMM)
        mv.op = X64_OP_FMOV;
    return mv;
}

static void repair_vreg(X64Func *f, u32 v)
{
    u8 rc = x64_vclass(f, v);
    u32 bi, i, k;

    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];
        Rb rb;

        rb_init(&rb, f->arena, b->n);
        for (i = 0; i < b->n; i++) {
            X64Inst in = b->insts[i];

            if (in.a.kind == X64O_VREG && in.a.fixed && in.a.r.v == v) {
                X64VReg t = x64_newv(f, rc);
                X64Inst mv = mk_copy(f, t, in.a.r, rc);

                rb_put(&rb, &mv);
                in.a.r = t;
            }
            if (in.b.kind == X64O_VREG && in.b.fixed && in.b.r.v == v) {
                X64VReg t = x64_newv(f, rc);
                X64Inst mv = mk_copy(f, t, in.b.r, rc);

                rb_put(&rb, &mv);
                in.b.r = t;
            }
            for (k = 0; k < in.nxuses; k++) {
                if (in.xuses[k].r.v == v && in.xuses[k].fixed) {
                    X64VReg t = x64_newv(f, rc);
                    X64Inst mv = mk_copy(f, t, in.xuses[k].r, rc);
                    X64XUse *nx =
                        arena_alloc(f->arena, in.nxuses * sizeof(X64XUse),
                                    _Alignof(X64XUse));

                    rb_put(&rb, &mv);
                    /* xuses is shared with the original inst; copy
                     * before retargeting. */
                    memcpy(nx, in.xuses, in.nxuses * sizeof(X64XUse));
                    nx[k].r = t;
                    in.xuses = nx;
                }
            }
            if (in.def.v == v && in.def_fixed) {
                X64VReg t = x64_newv(f, rc);
                X64VReg vv = {v};
                X64Inst mv = mk_copy(f, vv, t, rc);

                in.def = t;
                rb.map[i] = rb.n;
                rb_put(&rb, &in);
                rb_put(&rb, &mv);
                continue;
            }
            rb.map[i] = rb.n;
            rb_put(&rb, &in);
        }
        rb_commit(&rb, b);
    }
}

/* Two same-color fixed intervals overlapping = repair both. Sharing one
 * endpoint is legal (a value dying exactly where the next is defined —
 * the idiv handoff), so the test is strict interior overlap. */
static u32 find_conflicts(Ra *ra, u32 *out, u32 cap)
{
    const X64Func *f = ra->f;
    u32 n = 0;
    u32 v, v2;

    for (v = 1; v <= f->nvregs && n < cap; v++) {
        const Interval *a = &ra->iv[v];

        if (!a->live || !a->fixed)
            continue;
        for (v2 = v + 1; v2 <= f->nvregs && n + 1 < cap; v2++) {
            const Interval *b = &ra->iv[v2];

            if (!b->live || b->fixed != a->fixed)
                continue;
            if (a->start < b->end && b->start < a->end) {
                out[n++] = v;
                out[n++] = v2;
            }
        }
    }
    return n;
}

/* --- linear scan -----------------------------------------------------------
 */

static int iv_cmp_start(const void *pa, const void *pb, void *ctx)
{
    const Interval *x = *(Interval *const *)pa;
    const Interval *y = *(Interval *const *)pb;

    (void)ctx;
    if (x->start != y->start)
        return x->start < y->start ? -1 : 1;
    return x->vreg < y->vreg ? -1 : x->vreg > y->vreg ? 1 : 0;
}

static void assign_slot(Ra *ra, Interval *it)
{
    if (!it->slot) {
        ra->next_slot -= 8; /* 8-byte GP slots; 16-byte xmm in Sprint 23 */
        it->slot = ra->next_slot;
        ra->f->spill_slots++;
    }
}

/* Would giving [start,end] register `reg` collide with a fixed interval?
 * Inclusive overlap: even endpoint sharing is refused for ordinary
 * intervals, because the two-address rewrite can make a def live before
 * its instruction's reads (that hazard has its own fixup only between
 * the inst's OWN operands, not against bystanders). */
static bool fixed_clash(const Ra *ra, u8 reg, u32 start, u32 end)
{
    u32 v;

    for (v = 1; v <= ra->f->nvregs; v++) {
        const Interval *it = &ra->iv[v];

        if (!it->live || it->fixed != (u8)(reg + 1))
            continue;
        if (it->start <= end && start <= it->end)
            return true;
    }
    return false;
}

static void linear_scan(Ra *ra)
{
    X64Func *f = ra->f;
    Interval **order;
    Interval **active;
    u32 nactive = 0, nord = 0;
    u32 v, i, k;
    const char *env = cgf_env("CGF_SPILL_ALL");
    bool spill_all = env && strcmp(env, "1") == 0;

    order = arena_alloc(ra->arena, (f->nvregs + 1) * sizeof(Interval *),
                        _Alignof(Interval *));
    active = arena_alloc(ra->arena, (f->nvregs + 1) * sizeof(Interval *),
                         _Alignof(Interval *));
    for (v = 1; v <= f->nvregs; v++)
        if (ra->iv[v].live)
            order[nord++] = &ra->iv[v];
    cgf_sort_stable(order, nord, sizeof(Interval *), iv_cmp_start, NULL);

    for (i = 0; i < nord; i++) {
        Interval *cur = order[i];
        bool used[X64_REG_COUNT];

        /* Expire strictly-ended intervals. An interval ending exactly at
         * cur->start stays active — see fixed_clash on why endpoint
         * sharing is not safe for ordinary intervals. */
        for (k = 0; k < nactive;) {
            if (active[k]->end < cur->start)
                active[k] = active[--nactive];
            else
                k++;
        }
        if (cur->fixed) {
            /* Pre-colored: assigned unconditionally. Disjointness among
             * fixed intervals was repaired; ordinary intervals steer
             * around them via fixed_clash. */
            cur->phys = cur->fixed;
            active[nactive++] = cur;
            continue;
        }
        if (spill_all) {
            assign_slot(ra, cur);
            continue;
        }
        {
            u8 rc = x64_vclass(f, cur->vreg);
            bool crossing = crosses_call(ra, cur->start, cur->end);
            const u8 *pool = rc == X64RC_XMM ? xmm_order : alloc_order;
            u32 npool = rc == X64RC_XMM ? NXMM : NALLOC;

            /* Call-crossing xmm ALWAYS spills — no callee-saved xmm
             * exists, so there is no register that survives. */
            if (crossing && rc == X64RC_XMM) {
                assign_slot(ra, cur);
                continue;
            }
            memset(used, 0, sizeof(used));
            for (k = 0; k < nactive; k++)
                if (active[k]->phys)
                    used[active[k]->phys - 1] = true;
            cur->phys = 0;
            for (k = 0; k < npool; k++) {
                u8 r = pool[k];

                if (crossing && rc == X64RC_GP && !is_callee_saved(r))
                    continue;
                if (!used[r] && !fixed_clash(ra, r, cur->start, cur->end)) {
                    cur->phys = (u8)(r + 1);
                    break;
                }
            }
            if (!cur->phys) {
                /* Pressure: spill the active interval with the furthest
                 * end (the no-holes proxy for furthest next use) — if it
                 * ends later than cur and its register is legal for cur
                 * (same class, callee-saved when cur crosses a call). */
                Interval *far = NULL;

                for (k = 0; k < nactive; k++) {
                    Interval *cand = active[k];

                    if (cand->fixed || !cand->phys)
                        continue;
                    if (x64_vclass(f, cand->vreg) != rc)
                        continue;
                    if (crossing && rc == X64RC_GP &&
                        !is_callee_saved((u8)(cand->phys - 1)))
                        continue;
                    if (fixed_clash(ra, (u8)(cand->phys - 1), cur->start,
                                    cur->end))
                        continue;
                    if (!far || cand->end > far->end)
                        far = cand;
                }
                if (far && far->end > cur->end) {
                    cur->phys = far->phys;
                    far->phys = 0;
                    assign_slot(ra, far);
                    for (k = 0; k < nactive; k++)
                        if (active[k] == far) {
                            active[k] = cur;
                            break;
                        }
                } else {
                    assign_slot(ra, cur);
                }
                continue;
            }
            active[nactive++] = cur;
        }
    }
}

/* --- rewrite ---------------------------------------------------------------
 *
 * Substitute assignments (post-RA encoding: X64VReg.v = X64Reg + 1),
 * insert reloads/stores for spilled intervals, and expand the stack-op
 * markers. Spill traffic is always full-width: 32-bit defs zeroed their
 * upper bits, so a q-store captures exactly the zext MIR relies on.
 * Scratch discipline inside ONE instruction: a-side reloads and the def
 * store use r11; b-side reloads use r10 — so a spilled def aliasing its
 * own reloaded src1 is exactly the dst==src1 form the two-address fixup
 * wants, and a def can never alias a b-side reload. */

static X64VReg physreg(u8 reg)
{
    X64VReg r = {(u32)reg + 1};

    return r;
}

static X64Inst mk_reload(u8 reg, i32 slot, bool xmm)
{
    X64Inst m;

    memset(&m, 0, sizeof(m));
    m.op = xmm ? X64_OP_FLOAD : X64_OP_LOAD;
    m.width = X64_Q;
    m.def = physreg(reg);
    m.a.kind = X64O_MEM;
    m.a.mem.base = physreg(X64_RBP);
    m.a.mem.scale = 1;
    m.a.mem.disp = slot;
    return m;
}

static X64Inst mk_spill(u8 reg, i32 slot, bool xmm)
{
    X64Inst m;

    memset(&m, 0, sizeof(m));
    m.op = xmm ? X64_OP_FSTORE : X64_OP_STORE;
    m.width = X64_Q;
    m.a.kind = X64O_VREG;
    m.a.r = physreg(reg);
    m.b.kind = X64O_MEM;
    m.b.mem.base = physreg(X64_RBP);
    m.b.mem.scale = 1;
    m.b.mem.disp = slot;
    return m;
}

/* Substitute one use; emits a reload into the class's scratch first if
 * spilled. `side` 0 = a-side (r11/xmm15), 1 = b-side (r10/xmm14). Mem
 * base/index registers are always GP regardless of side value class. */
static void sub_use(Ra *ra, Rb *rb, X64VReg *r, int side)
{
    Interval *it;
    u8 rc;

    if (!r->v)
        return;
    rc = x64_vclass(ra->f, r->v);
    it = &ra->iv[r->v];
    if (it->phys) {
        r->v = it->phys;
        return;
    }
    {
        u8 s = rc == X64RC_XMM ? (side ? SCRATCH_XB : SCRATCH_XA)
                               : (side ? SCRATCH_B : SCRATCH_A);
        X64Inst ld = mk_reload(s, it->slot, rc == X64RC_XMM);

        rb_put(rb, &ld);
        r->v = (u32)s + 1;
    }
}

static void rewrite(Ra *ra)
{
    X64Func *f = ra->f;
    u32 bi, i;

    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];
        Rb rb;

        rb_init(&rb, f->arena, b->n);
        for (i = 0; i < b->n; i++) {
            X64Inst in = b->insts[i];
            Interval *dit = in.def.v ? &ra->iv[in.def.v] : NULL;

            /* uses first: reloads sit before the instruction */
            if (in.a.kind == X64O_VREG)
                sub_use(ra, &rb, &in.a.r, 0);
            if (in.b.kind == X64O_VREG)
                sub_use(ra, &rb, &in.b.r, 1);
            if (in.a.kind == X64O_MEM) {
                sub_use(ra, &rb, &in.a.mem.base, 0);
                sub_use(ra, &rb, &in.a.mem.index, 1);
                if (in.a.mem.rsp_rel) {
                    /* outgoing-arg slot: the base IS rsp (post-RA it has
                     * a spelling; substitution above saw base 0). */
                    in.a.mem.base = physreg(X64_RSP);
                    in.a.mem.rsp_rel = 0;
                }
            }
            if (in.b.kind == X64O_MEM) {
                /* Both b-mem components share r10 (r11 is the a-side's),
                 * so both spilled at once has no scratch left. isel only
                 * ever folds base+disp into store addresses, never
                 * base+index — this trips if that changes. */
                bool base_sp = in.b.mem.base.v && !ra->iv[in.b.mem.base.v].phys;
                bool idx_sp =
                    in.b.mem.index.v && !ra->iv[in.b.mem.index.v].phys;

                if (base_sp && idx_sp)
                    CGF_ICE("regalloc: spilled base + spilled index in "
                            "one b-side mem operand");
                sub_use(ra, &rb, &in.b.mem.base, 1);
                sub_use(ra, &rb, &in.b.mem.index, 1);
                if (in.b.mem.rsp_rel) {
                    in.b.mem.base = physreg(X64_RSP);
                    in.b.mem.rsp_rel = 0;
                }
            }
            in.a.fixed = 0;
            in.b.fixed = 0;
            in.xuses = NULL; /* implicit in the encoding post-RA */
            in.nxuses = 0;

            /* marker expansion (operands above are already physical) */
            if (in.op == X64_OP_STACKSAVE) {
                rb.map[i] = rb.n;
                if (dit->phys) {
                    X64Inst mv =
                        mk_mov(physreg((u8)(dit->phys - 1)), physreg(X64_RSP));

                    rb_put(&rb, &mv);
                } else {
                    X64Inst st = mk_spill(X64_RSP, dit->slot, false);

                    rb_put(&rb, &st);
                }
                continue;
            }
            if (in.op == X64_OP_READREG) {
                /* def <- current content of the def_fixed register:
                 * incoming argument or post-call second result. When
                 * allocation landed the def right there, nothing moves. */
                u8 src = (u8)(in.def_fixed - 1);
                bool xmm = src >= X64_XMM0;

                rb.map[i] = rb.n;
                if (dit->phys && dit->phys == in.def_fixed) {
                    /* already home: no instruction at all */
                } else if (dit->phys) {
                    X64Inst mv =
                        mk_mov(physreg((u8)(dit->phys - 1)), physreg(src));

                    if (xmm)
                        mv.op = X64_OP_FMOV;
                    rb_put(&rb, &mv);
                } else {
                    X64Inst st = mk_spill(src, dit->slot, xmm);

                    rb_put(&rb, &st);
                }
                continue;
            }
            if (in.op == X64_OP_ARGLD || in.op == X64_OP_ARGLEA) {
                /* incoming stack argument at [rbp + 16 + b.imm] — load
                 * its value (ARGLD, class-typed) or its address
                 * (ARGLEA: byval aggregates and f80s live in caller
                 * frame; the param IS that address). */
                bool xmm = dit && x64_vclass(f, b->insts[i].def.v) == X64RC_XMM;
                X64Inst x;

                memset(&x, 0, sizeof(x));
                x.op = in.op == X64_OP_ARGLEA ? X64_OP_LEA
                       : xmm                  ? X64_OP_FLOAD
                                              : X64_OP_LOAD;
                x.width = in.width ? in.width : X64_Q;
                x.a.kind = X64O_MEM;
                x.a.mem.base = physreg(X64_RBP);
                x.a.mem.scale = 1;
                x.a.mem.disp = (i32)(16 + in.b.imm);
                rb.map[i] = rb.n;
                if (dit->phys) {
                    x.def.v = dit->phys;
                    rb_put(&rb, &x);
                } else {
                    u8 s = xmm ? SCRATCH_XA : SCRATCH_A;
                    X64Inst st = mk_spill(s, dit->slot, xmm);

                    x.def = physreg(s);
                    rb_put(&rb, &x);
                    rb_put(&rb, &st);
                }
                continue;
            }
            if (in.op == X64_OP_STACKRESTORE) {
                X64Inst mv;

                memset(&mv, 0, sizeof(mv));
                mv.op = X64_OP_MOV;
                mv.width = X64_Q;
                mv.def = physreg(X64_RSP);
                mv.a = in.a;
                rb.map[i] = rb.n;
                rb_put(&rb, &mv);
                continue;
            }
            if (in.op == X64_OP_ALLOCA_DYN) {
                /* mov r10, size; add r10, 15; and r10, -16;
                 * sub rsp, r10; def = rsp. Emitted in canonical
                 * two-address form (def == a) so the fixup skips it. */
                X64Inst x;

                rb.map[i] = rb.n;
                memset(&x, 0, sizeof(x));
                x.op = X64_OP_MOV;
                x.width = X64_Q;
                x.def = physreg(SCRATCH_B);
                x.a = in.a;
                rb_put(&rb, &x);
                memset(&x, 0, sizeof(x));
                x.op = X64_OP_ADD;
                x.width = X64_Q;
                x.flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
                x.def = physreg(SCRATCH_B);
                x.a.kind = X64O_VREG;
                x.a.r = physreg(SCRATCH_B);
                x.b.kind = X64O_IMM;
                x.b.imm = 15;
                rb_put(&rb, &x);
                x.op = X64_OP_AND;
                x.b.imm = -16;
                rb_put(&rb, &x);
                memset(&x, 0, sizeof(x));
                x.op = X64_OP_SUB;
                x.width = X64_Q;
                x.flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
                x.def = physreg(X64_RSP);
                x.a.kind = X64O_VREG;
                x.a.r = physreg(X64_RSP);
                x.b.kind = X64O_VREG;
                x.b.r = physreg(SCRATCH_B);
                rb_put(&rb, &x);
                if (dit->phys) {
                    X64Inst mv =
                        mk_mov(physreg((u8)(dit->phys - 1)), physreg(X64_RSP));

                    rb_put(&rb, &mv);
                } else {
                    X64Inst st = mk_spill(X64_RSP, dit->slot, false);

                    rb_put(&rb, &st);
                }
                continue;
            }

            /* ordinary instruction: substitute the def, store if spilled */
            rb.map[i] = rb.n;
            if (dit && dit->phys) {
                in.def.v = dit->phys;
                in.def_fixed = 0;
                rb_put(&rb, &in);
            } else if (dit) {
                bool xmm = x64_vclass(f, in.def.v) == X64RC_XMM;
                u8 s = xmm ? SCRATCH_XA : SCRATCH_A;
                X64Inst st = mk_spill(s, dit->slot, xmm);

                in.def = physreg(s);
                in.def_fixed = 0;
                rb_put(&rb, &in);
                rb_put(&rb, &st);
            } else {
                rb_put(&rb, &in);
            }
        }
        rb_commit(&rb, b);
    }
}

/* --- two-address fixup -----------------------------------------------------
 *
 * On ALLOCATED MIR: `def = op src1, src2` becomes the x86 form
 * `def == a` with the dst==src2 hazard resolved:
 *   dst == src1                  -> already canonical
 *   dst == src2, commutative     -> swap:  op dst, src1
 *   dst == src2, non-commutative -> mov r10, src2; mov dst, src1;
 *                                   op dst, r10
 *   otherwise                    -> mov dst, src1; op dst, src2
 * r10 is safe in the rescue: the only way src2 sits in r10 is a b-side
 * reload, and then dst (r11 or an allocated register) cannot equal it. */

static bool op_commutative(u16 op)
{
    /* IEEE addition and multiplication commute (addsd/mulsd included);
     * FP subtraction and division do not. */
    return op == X64_OP_ADD || op == X64_OP_AND || op == X64_OP_OR ||
           op == X64_OP_XOR || op == X64_OP_IMUL || op == X64_OP_FADD ||
           op == X64_OP_FMUL;
}

/* Copy between two PHYSICAL ids, picking mov vs movsd by bank. */
static X64Inst mk_pcopy(u32 dst, u32 src)
{
    X64VReg d = {dst};
    X64VReg s = {src};
    X64Inst mv = mk_mov(d, s);

    if (dst - 1 >= X64_XMM0)
        mv.op = X64_OP_FMOV;
    return mv;
}

void x64_twoaddr_fixup(X64Func *f)
{
    u32 bi, i;

    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];
        Rb rb;

        rb_init(&rb, f->arena, b->n);
        for (i = 0; i < b->n; i++) {
            X64Inst in = b->insts[i];
            u32 dst, src1, src2;
            bool xmm;

            rb.map[i] = rb.n;
            if (!(in.flags & X64IF_TWO_ADDR) || !in.def.v ||
                in.a.kind != X64O_VREG) {
                rb_put(&rb, &in);
                continue;
            }
            dst = in.def.v;
            src1 = in.a.r.v;
            src2 = in.b.kind == X64O_VREG ? in.b.r.v : 0;
            xmm = dst - 1 >= X64_XMM0;
            if (dst == src1) {
                rb_put(&rb, &in);
                continue;
            }
            if (src2 == dst && op_commutative((u16)in.op)) {
                in.a.r.v = dst;
                in.b.r.v = src1;
                rb_put(&rb, &in);
                continue;
            }
            if (src2 == dst) {
                u8 s = xmm ? SCRATCH_XB : SCRATCH_B;
                X64Inst mv = mk_pcopy((u32)s + 1, src2);

                rb_put(&rb, &mv);
                mv = mk_pcopy(dst, src1);
                rb_put(&rb, &mv);
                in.a.r.v = dst;
                in.b.r = physreg(s);
                rb_put(&rb, &in);
                continue;
            }
            {
                X64Inst mv = mk_pcopy(dst, src1);

                rb_put(&rb, &mv);
                in.a.r.v = dst;
                rb_put(&rb, &in);
            }
        }
        rb_commit(&rb, b);
    }
}

/* --- frame ------------------------------------------------------------------
 *
 * rbp frames ALWAYS in v0.1.0 (Sprint 29's CFI stays trivial); the red
 * zone is deliberately unused. Alignment LAW: rsp = 0 (mod 16) at every
 * call; entry is rsp = 8 (push rbp makes it 0; each callee-saved push
 * flips by 8; sub rsp,N restores 0). Misalignment does not fault here —
 * it faults as a movaps crash inside libc, far from the cause, which is
 * why the law is asserted rather than trusted. */

u32 x64_frame_align_pad(u32 pushes_after_rbp, u32 raw_bytes)
{
    u32 need = (pushes_after_rbp % 2) ? 8u : 0u;
    u32 n = (raw_bytes + 7) & ~7u;

    if ((n % 16) != need)
        n += 8;
    if (((n + 8 * pushes_after_rbp) % 16) != 0)
        CGF_ICE("frame alignment law violated: pushes=%u N=%u",
                pushes_after_rbp, n);
    return n;
}

static void frame_finalize(Ra *ra)
{
    X64Func *f = ra->f;
    bool touched[X64_REG_COUNT];
    u8 push_order[8];
    u32 npush = 0, bi, i, k;
    u32 raw = (u32)(-ra->next_slot);
    u32 save_off = 0; /* variadic reg-save area rbp-offset */

    memset(touched, 0, sizeof(touched));
    for (bi = 0; bi < f->nblocks; bi++) {
        const X64Block *b = &f->blocks[bi];

        for (i = 0; i < b->n; i++)
            if (b->insts[i].def.v && b->insts[i].def.v <= X64_REG_COUNT)
                touched[b->insts[i].def.v - 1] = true;
    }
    for (k = 0; k < X64_REG_COUNT; k++)
        if (touched[k] && is_callee_saved((u8)k))
            push_order[npush++] = (u8)k;

    /* Static alloca markers -> rbp-relative slots below the spills. rbp
     * itself is 16-aligned (entry rsp = 8 mod 16, push rbp lands on 0),
     * so rounding the running total to the alloca's align aligns the
     * slot address for every align <= 16. */
    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];

        for (i = 0; i < b->n; i++) {
            X64Inst *in = &b->insts[i];

            if (in->op == X64_OP_LEA && in->a.kind == X64O_MEM &&
                !in->a.mem.base.v && !in->a.mem.index.v && !in->a.mem.rip_sym &&
                !in->a.mem.cpool) {
                u32 size = in->b.imm > 0 ? (u32)in->b.imm : 1;
                u32 align = in->table ? in->table : 1;

                raw = (raw + size + align - 1) & ~(align - 1);
                in->a.mem.base = physreg(X64_RBP);
                in->a.mem.disp = -(i32)raw;
                in->b.imm = 0;
                in->table = 0;
            }
        }
    }

    /* Variadic: the psABI register save area (176 bytes, 16-aligned)
     * sits below the locals; va_start's reg_save_area points at its
     * base. The prologue stores rdi..r9 at +0..40 and xmm0-7 at
     * +48..160. We store the xmm slots UNCONDITIONALLY (movsd) rather
     * than gating on AL like gcc: the va_arg expansion never reads a
     * slot whose argument was not passed, so the gate is a skipped-work
     * optimization, not correctness — call sites still SET AL for
     * external callees that do consult it. */
    if (f->variadic) {
        raw = ((raw + 15) & ~15u) + 176;
        save_off = raw;
    }
    /* Outgoing call arguments live at the stack bottom, [rsp+0 ..
     * rsp+out_args); rsp-relative operands were rewritten to rsp, so a
     * dynamic alloca between frame setup and a call re-establishes the
     * area below itself automatically. */
    f->frame_size = x64_frame_align_pad(npush, raw + f->out_args);

    /* va_start expansion: only frame-finalize knows the two addresses
     * the IR op owes the va_list (overflow_arg_area = first UNNAMED
     * stack argument; reg_save_area = the base laid out above). */
    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];
        Rb rb;
        bool any = false;

        for (i = 0; i < b->n; i++)
            if (b->insts[i].op == X64_OP_VASTART)
                any = true;
        if (!any)
            continue;
        rb_init(&rb, f->arena, b->n);
        for (i = 0; i < b->n; i++) {
            X64Inst in = b->insts[i];
            X64Inst x;
            X64Operand ap;

            if (in.op != X64_OP_VASTART) {
                rb.map[i] = rb.n;
                rb_put(&rb, &in);
                continue;
            }
            ap = in.a;
            rb.map[i] = rb.n;
            /* lea r10, [rbp + 16 + named]; mov [ap+8], r10 */
            memset(&x, 0, sizeof(x));
            x.op = X64_OP_LEA;
            x.width = X64_Q;
            x.def = physreg(SCRATCH_B);
            x.a.kind = X64O_MEM;
            x.a.mem.base = physreg(X64_RBP);
            x.a.mem.scale = 1;
            x.a.mem.disp = (i32)(16 + f->named_stack_bytes);
            rb_put(&rb, &x);
            memset(&x, 0, sizeof(x));
            x.op = X64_OP_STORE;
            x.width = X64_Q;
            x.a.kind = X64O_VREG;
            x.a.r = physreg(SCRATCH_B);
            x.b.kind = X64O_MEM;
            x.b.mem.base = ap.r;
            x.b.mem.scale = 1;
            x.b.mem.disp = 8;
            rb_put(&rb, &x);
            /* lea r10, [rbp - save_off]; mov [ap+16], r10 */
            memset(&x, 0, sizeof(x));
            x.op = X64_OP_LEA;
            x.width = X64_Q;
            x.def = physreg(SCRATCH_B);
            x.a.kind = X64O_MEM;
            x.a.mem.base = physreg(X64_RBP);
            x.a.mem.scale = 1;
            x.a.mem.disp = -(i32)save_off;
            rb_put(&rb, &x);
            memset(&x, 0, sizeof(x));
            x.op = X64_OP_STORE;
            x.width = X64_Q;
            x.a.kind = X64O_VREG;
            x.a.r = physreg(SCRATCH_B);
            x.b.kind = X64O_MEM;
            x.b.mem.base = ap.r;
            x.b.mem.scale = 1;
            x.b.mem.disp = 16;
            rb_put(&rb, &x);
        }
        rb_commit(&rb, b);
    }

    /* Prologue: prepended to block 0. flags_src offsets in block 0 shift
     * for producer and consumer equally, so the map keeps them honest. */
    {
        X64Block *b = &f->blocks[0];
        Rb rb;
        X64Inst p;

        rb_init(&rb, f->arena, b->n);
        memset(&p, 0, sizeof(p));
        p.op = X64_OP_PUSH;
        p.width = X64_Q;
        p.a.kind = X64O_VREG;
        p.a.r = physreg(X64_RBP);
        rb_put(&rb, &p);
        p = mk_mov(physreg(X64_RBP), physreg(X64_RSP));
        rb_put(&rb, &p);
        for (k = 0; k < npush; k++) {
            memset(&p, 0, sizeof(p));
            p.op = X64_OP_PUSH;
            p.width = X64_Q;
            p.a.kind = X64O_VREG;
            p.a.r = physreg(push_order[k]);
            rb_put(&rb, &p);
        }
        if (f->frame_size) {
            memset(&p, 0, sizeof(p));
            p.op = X64_OP_SUB;
            p.width = X64_Q;
            p.flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
            p.def = physreg(X64_RSP);
            p.a.kind = X64O_VREG;
            p.a.r = physreg(X64_RSP);
            p.b.kind = X64O_IMM;
            p.b.imm = f->frame_size;
            rb_put(&rb, &p);
        }
        if (f->variadic) {
            static const u8 gpargs[6] = {X64_RDI, X64_RSI, X64_RDX,
                                         X64_RCX, X64_R8,  X64_R9};

            for (k = 0; k < 6; k++) {
                p = mk_spill(gpargs[k], -(i32)save_off + (i32)(8 * k), false);
                rb_put(&rb, &p);
            }
            for (k = 0; k < 8; k++) {
                p = mk_spill((u8)(X64_XMM0 + k),
                             -(i32)save_off + (i32)(48 + 16 * k), true);
                rb_put(&rb, &p);
            }
        }
        for (i = 0; i < b->n; i++) {
            rb.map[i] = rb.n;
            rb_put(&rb, &b->insts[i]);
        }
        rb_commit(&rb, b);
    }
    /* Epilogue before every ret. `mov rsp, rbp` would strand the
     * callee-saved pushes; `lea rsp, [rbp - 8*npush]` points at the
     * lowest push regardless of any dynamic alloca, then pops unwind in
     * reverse. */
    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];
        Rb rb;
        bool any = false;

        for (i = 0; i < b->n; i++)
            if (b->insts[i].op == X64_OP_RET)
                any = true;
        if (!any)
            continue;
        rb_init(&rb, f->arena, b->n);
        for (i = 0; i < b->n; i++) {
            X64Inst p;

            if (b->insts[i].op != X64_OP_RET) {
                rb.map[i] = rb.n;
                rb_put(&rb, &b->insts[i]);
                continue;
            }
            memset(&p, 0, sizeof(p));
            p.op = X64_OP_LEA;
            p.width = X64_Q;
            p.def = physreg(X64_RSP);
            p.a.kind = X64O_MEM;
            p.a.mem.base = physreg(X64_RBP);
            p.a.mem.scale = 1;
            p.a.mem.disp = -(i32)(8 * npush);
            rb_put(&rb, &p);
            for (k = npush; k-- > 0;) {
                memset(&p, 0, sizeof(p));
                p.op = X64_OP_POP;
                p.width = X64_Q;
                p.def = physreg(push_order[k]);
                rb_put(&rb, &p);
            }
            memset(&p, 0, sizeof(p));
            p.op = X64_OP_POP;
            p.width = X64_Q;
            p.def = physreg(X64_RBP);
            rb_put(&rb, &p);
            rb.map[i] = rb.n;
            rb_put(&rb, &b->insts[i]);
        }
        rb_commit(&rb, b);
    }
}

/* --- entry ------------------------------------------------------------------
 */

void x64_regalloc(X64Func *f)
{
    Ra ra;
    u32 iter;

    memset(&ra, 0, sizeof(ra));
    ra.f = f;
    ra.arena = f->arena;

    /* Build + repair to a conflict-free precoloring. Each repair strictly
     * localizes constraints to per-site tiny intervals, so this
     * converges; the cap is a tripwire, not a tuning knob. */
    for (iter = 0;; iter++) {
        u32 conflicts[64], nc, j;

        if (iter > 16)
            CGF_ICE("regalloc: precolor repair did not converge");
        build_intervals(&ra);
        collect_fixed(&ra);
        nc = find_conflicts(&ra, conflicts, 64);
        if (!nc)
            break;
        for (j = 0; j < nc; j++) {
            bool dup = false;
            u32 j2;

            for (j2 = 0; j2 < j; j2++)
                if (conflicts[j2] == conflicts[j])
                    dup = true;
            if (!dup)
                repair_vreg(f, conflicts[j]);
        }
    }

    linear_scan(&ra);
    rewrite(&ra);
    x64_twoaddr_fixup(f);
    frame_finalize(&ra);
    f->allocated = true;
}

X64RegallocFn x64_regalloc_entry(CgOptLevel level)
{
    /* THE invariant: one allocator, every level. */
    (void)level;
    return x64_regalloc;
}
