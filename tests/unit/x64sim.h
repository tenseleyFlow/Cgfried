#ifndef CGF_TEST_X64SIM_H
#define CGF_TEST_X64SIM_H

#include <math.h>
#include <string.h>

#include "cg/cg.h"
#include "unit.h"

/* The tiny MIR interpreter (Sprint 22, grown FP/x87/branches in Sprint
 * 23) — the allocator's and FP isel's secret weapon. It executes vreg
 * form and post-RA form alike (registers are just ids 1..32), models
 * EFLAGS from cmp/test/ucomi/fucomip, follows branches, walks the x87
 * stack with host long doubles (the unit suite runs on x86-64, where
 * host long double IS the 80-bit format), and honors the RC field for
 * fistp — so the compiled RC dance is what makes truncation happen in
 * the sim too.
 *
 * Constant-pool entries materialize at SIM_CPOOL_BASE; markers
 * (readreg/argld/...) are post-RA-only concepts and are rejected. */

#define SIM_NV 512
#define SIM_MEM (1u << 16)
#define SIM_CPOOL_BASE 0xE000u
#define SIM_MAX_STEPS 100000

typedef struct SimFlags {
    bool zf, pf, cf, sf, of;
} SimFlags;

typedef struct Sim {
    u64 val[SIM_NV];
    u8 *mem;
    SimFlags fl;
    long double st[8];
    int nst;
    u16 fcw;
} Sim;

static inline u64 sim_sext(u64 v, u8 bytes)
{
    if (bytes == 1)
        return (u64)(i64)(i8)v;
    if (bytes == 2)
        return (u64)(i64)(i16)v;
    if (bytes == 4)
        return (u64)(i64)(i32)v;
    return v;
}

static inline u64 sim_mask(u64 v, u8 w)
{
    return w >= 8 ? v : v & ((1ull << (w * 8)) - 1);
}

static inline double sim_f64(u64 bits)
{
    double d;

    memcpy(&d, &bits, 8);
    return d;
}

static inline u64 sim_f64bits(double d)
{
    u64 b;

    memcpy(&b, &d, 8);
    return b;
}

static inline float sim_f32(u64 bits)
{
    float f;
    u32 lo = (u32)bits;

    memcpy(&f, &lo, 4);
    return f;
}

static inline u64 sim_f32bits(float f)
{
    u32 b;

    memcpy(&b, &f, 4);
    return (u64)b;
}

static inline u64 sim_addr(TestCtx *t, const Sim *s, const X64Mem *m)
{
    u64 a = 0;

    T_ASSERT(t, !m->rip_sym);
    T_ASSERT(t, !m->rsp_rel);
    if (m->cpool)
        return SIM_CPOOL_BASE + (u64)(m->cpool - 1) * 16;
    if (m->base.v)
        a += s->val[m->base.v];
    if (m->index.v)
        a += s->val[m->index.v] * m->scale;
    a += (u64)(i64)m->disp;
    return a;
}

static inline u64 sim_rdmem(TestCtx *t, const Sim *s, u64 addr, u8 w)
{
    u64 v = 0;
    u8 i;

    T_ASSERT(t, addr + w <= SIM_MEM);
    for (i = 0; i < w; i++)
        v |= (u64)s->mem[addr + i] << (8 * i);
    return v;
}

static inline void sim_wrmem(TestCtx *t, Sim *s, u64 addr, u64 v, u8 w)
{
    u8 i;

    T_ASSERT(t, addr + w <= SIM_MEM);
    for (i = 0; i < w; i++)
        s->mem[addr + i] = (u8)(v >> (8 * i));
}

static inline u64 sim_src(TestCtx *t, const Sim *s, const X64Operand *o, u8 w)
{
    switch (o->kind) {
    case X64O_VREG:
        return s->val[o->r.v];
    case X64O_IMM:
        return (u64)o->imm;
    case X64O_MEM:
        return sim_rdmem(t, s, sim_addr(t, s, &o->mem), w > 8 ? 8 : w);
    default:
        T_ASSERT(t, false);
        return 0;
    }
}

static inline bool sim_cc(TestCtx *t, const SimFlags *f, u8 cc)
{
    switch (cc) {
    case X64_CC_E:
        return f->zf;
    case X64_CC_NE:
        return !f->zf;
    case X64_CC_L:
        return f->sf != f->of;
    case X64_CC_LE:
        return f->zf || f->sf != f->of;
    case X64_CC_G:
        return !f->zf && f->sf == f->of;
    case X64_CC_GE:
        return f->sf == f->of;
    case X64_CC_B:
        return f->cf;
    case X64_CC_BE:
        return f->cf || f->zf;
    case X64_CC_A:
        return !f->cf && !f->zf;
    case X64_CC_AE:
        return !f->cf;
    case X64_CC_P:
        return f->pf;
    case X64_CC_NP:
        return !f->pf;
    default:
        T_ASSERT(t, false);
        return false;
    }
}

static inline void sim_cmp_flags(Sim *s, u64 a, u64 b, u8 w)
{
    u64 r = sim_mask(a - b, w);
    u64 sa = sim_sext(sim_mask(a, w), w);
    u64 sb = sim_sext(sim_mask(b, w), w);
    i64 wide = (i64)sa - (i64)sb;
    i64 narrow = (i64)sim_sext(r, w);

    s->fl.zf = r == 0;
    s->fl.cf = sim_mask(a, w) < sim_mask(b, w);
    s->fl.sf = (narrow < 0);
    s->fl.of = wide != narrow;
    s->fl.pf = false; /* integer PF unused by our isel */
}

static inline void sim_fcmp_flags(Sim *s, long double a, long double b)
{
    if (isunordered(a, b)) {
        s->fl.zf = s->fl.pf = s->fl.cf = true;
    } else {
        s->fl.zf = a == b;
        s->fl.cf = a < b;
        s->fl.pf = false;
    }
    s->fl.sf = s->fl.of = false;
}

static inline long double sim_ld_rd(TestCtx *t, Sim *s, u64 addr, u8 w)
{
    if (w == 10) {
        long double v = 0;

        T_ASSERT(t, addr + 10 <= SIM_MEM);
        memcpy(&v, s->mem + addr, 10);
        return v;
    }
    if (w == 8)
        return (long double)sim_f64(sim_rdmem(t, s, addr, 8));
    return (long double)sim_f32(sim_rdmem(t, s, addr, 4));
}

static inline void sim_ld_wr(TestCtx *t, Sim *s, u64 addr, long double v, u8 w)
{
    if (w == 10) {
        T_ASSERT(t, addr + 10 <= SIM_MEM);
        memcpy(s->mem + addr, &v, 10);
        return;
    }
    if (w == 8)
        sim_wrmem(t, s, addr, sim_f64bits((double)v), 8);
    else
        sim_wrmem(t, s, addr, sim_f32bits((float)v), 4);
}

static inline void sim_init(Sim *s, u8 *mem, const X64Func *f)
{
    u32 i;

    memset(s->val, 0, sizeof(s->val));
    memset(&s->fl, 0, sizeof(s->fl));
    s->nst = 0;
    s->fcw = 0x037f;
    s->mem = mem;
    memset(mem, 0, SIM_MEM);
    s->val[X64_RSP + 1] = SIM_MEM - 512; /* stack top */
    for (i = 0; i < f->nconsts; i++) {
        u64 base = SIM_CPOOL_BASE + (u64)i * 16;
        u32 k;

        for (k = 0; k < 8; k++)
            mem[base + k] = (u8)(f->consts[i].lo >> (8 * k));
        for (k = 0; k < 8; k++)
            mem[base + 8 + k] = (u8)(f->consts[i].hi >> (8 * k));
    }
}

/* Executes from block 0 until RET. Returns false on an op the model
 * does not cover or when the step budget runs out. */
static inline bool sim_run(TestCtx *t, const X64Func *f, Sim *s)
{
    u32 bi = 0, i = 0, steps = 0;

    while (steps++ < SIM_MAX_STEPS) {
        const X64Block *b = &f->blocks[bi];
        const X64Inst *in;
        u8 w;
        u64 av, bv, r;

        if (i >= b->n) {
            /* The post-RA peephole removes an unconditional jump to the next
             * layout block.  That is a real machine-code fallthrough, so the
             * MIR simulator must follow it too.  Falling off the final block
             * remains malformed/nonterminating input. */
            if (bi + 1 >= f->nblocks)
                return false;
            bi++;
            i = 0;
            continue;
        }
        in = &b->insts[i++];
        w = in->width;
        switch (in->op) {
        case X64_OP_MOV:
            s->val[in->def.v] = sim_mask(sim_src(t, s, &in->a, w), w);
            break;
        case X64_OP_MOVABS:
            s->val[in->def.v] = (u64)in->a.imm;
            break;
        case X64_OP_MOVZX:
            s->val[in->def.v] =
                in->a.kind == X64O_MEM
                    ? sim_rdmem(t, s, sim_addr(t, s, &in->a.mem), in->src_width)
                    : sim_mask(s->val[in->a.r.v], in->src_width);
            break;
        case X64_OP_MOVSX:
            s->val[in->def.v] = sim_mask(
                sim_sext(in->a.kind == X64O_MEM
                             ? sim_rdmem(t, s, sim_addr(t, s, &in->a.mem),
                                         in->src_width)
                             : s->val[in->a.r.v],
                         in->src_width),
                w);
            break;
        case X64_OP_LEA:
            s->val[in->def.v] = sim_addr(t, s, &in->a.mem);
            break;
        case X64_OP_ADD:
        case X64_OP_SUB:
        case X64_OP_AND:
        case X64_OP_OR:
        case X64_OP_XOR:
        case X64_OP_IMUL:
            av = sim_src(t, s, &in->a, w);
            bv = sim_src(t, s, &in->b, w);
            r = in->op == X64_OP_ADD   ? av + bv
                : in->op == X64_OP_SUB ? av - bv
                : in->op == X64_OP_AND ? (av & bv)
                : in->op == X64_OP_OR  ? (av | bv)
                : in->op == X64_OP_XOR ? (av ^ bv)
                                       : av * bv;
            r = sim_mask(r, w);
            s->val[in->def.v] = r;
            s->fl.zf = r == 0;
            s->fl.sf = (i64)sim_sext(r, w) < 0;
            s->fl.cf = s->fl.of = s->fl.pf = false;
            break;
        case X64_OP_NEG:
            s->val[in->def.v] = sim_mask(0 - sim_src(t, s, &in->a, w), w);
            break;
        case X64_OP_NOT:
            s->val[in->def.v] = sim_mask(~sim_src(t, s, &in->a, w), w);
            break;
        case X64_OP_SHL:
        case X64_OP_SHR:
        case X64_OP_SAR: {
            u32 cnt = (u32)(sim_src(t, s, &in->b, w) & (w == 8 ? 63 : 31));

            av = sim_mask(sim_src(t, s, &in->a, w), w);
            if (in->op == X64_OP_SHL)
                r = av << cnt;
            else if (in->op == X64_OP_SHR)
                r = av >> cnt;
            else
                r = (u64)((i64)sim_sext(av, w) >> cnt);
            s->val[in->def.v] = sim_mask(r, w);
            break;
        }
        case X64_OP_CMP:
            sim_cmp_flags(s, sim_src(t, s, &in->a, w), sim_src(t, s, &in->b, w),
                          w);
            break;
        case X64_OP_TEST: {
            u64 x = sim_mask(
                sim_src(t, s, &in->a, w) & sim_src(t, s, &in->b, w), w);

            s->fl.zf = x == 0;
            s->fl.sf = (i64)sim_sext(x, w) < 0;
            s->fl.cf = s->fl.of = s->fl.pf = false;
            break;
        }
        case X64_OP_SETCC:
            s->val[in->def.v] = sim_cc(t, &s->fl, in->cc) ? 1 : 0;
            break;
        case X64_OP_LOAD:
            s->val[in->def.v] = sim_rdmem(t, s, sim_addr(t, s, &in->a.mem), w);
            break;
        case X64_OP_STORE:
            sim_wrmem(t, s, sim_addr(t, s, &in->b.mem),
                      sim_src(t, s, &in->a, w), w);
            break;
        case X64_OP_PUSH:
            s->val[X64_RSP + 1] -= 8;
            sim_wrmem(t, s, s->val[X64_RSP + 1], s->val[in->a.r.v], 8);
            break;
        case X64_OP_POP:
            s->val[in->def.v] = sim_rdmem(t, s, s->val[X64_RSP + 1], 8);
            s->val[X64_RSP + 1] += 8;
            break;
        case X64_OP_JMP:
            bi = in->target - 1;
            i = 0;
            break;
        case X64_OP_JCC:
            if (sim_cc(t, &s->fl, in->cc)) {
                bi = in->target - 1;
                i = 0;
            } else if (in->target2) {
                bi = in->target2 - 1;
                i = 0;
            } /* else: mid-block fallthrough */
            break;
        case X64_OP_RET:
            return true;
        /* --- SSE scalar FP (values are BITS in val[]) ------------------ */
        case X64_OP_FMOV:
            s->val[in->def.v] = s->val[in->a.r.v];
            break;
        case X64_OP_FLOAD:
            s->val[in->def.v] = sim_rdmem(t, s, sim_addr(t, s, &in->a.mem), w);
            break;
        case X64_OP_FSTORE:
            sim_wrmem(t, s, sim_addr(t, s, &in->b.mem), s->val[in->a.r.v], w);
            break;
        case X64_OP_FADD:
        case X64_OP_FSUB:
        case X64_OP_FMUL:
        case X64_OP_FDIV: {
            av = s->val[in->a.r.v];
            bv = sim_src(t, s, &in->b, w);
            if (w == 8) {
                double x = sim_f64(av), y = sim_f64(bv);
                double z = in->op == X64_OP_FADD   ? x + y
                           : in->op == X64_OP_FSUB ? x - y
                           : in->op == X64_OP_FMUL ? x * y
                                                   : x / y;

                s->val[in->def.v] = sim_f64bits(z);
            } else {
                float x = sim_f32(av), y = sim_f32(bv);
                float z = in->op == X64_OP_FADD   ? x + y
                          : in->op == X64_OP_FSUB ? x - y
                          : in->op == X64_OP_FMUL ? x * y
                                                  : x / y;

                s->val[in->def.v] = sim_f32bits(z);
            }
            break;
        }
        case X64_OP_UCOMI:
            av = s->val[in->a.r.v];
            bv = s->val[in->b.r.v];
            if (w == 8)
                sim_fcmp_flags(s, (long double)sim_f64(av),
                               (long double)sim_f64(bv));
            else
                sim_fcmp_flags(s, (long double)sim_f32(av),
                               (long double)sim_f32(bv));
            break;
        case X64_OP_CVTF2I: {
            long double x = in->src_width == 8
                                ? (long double)sim_f64(s->val[in->a.r.v])
                                : (long double)sim_f32(s->val[in->a.r.v]);
            i64 v = (i64)x; /* host truncates, matching cvtts*2si */

            s->val[in->def.v] = sim_mask((u64)v, w);
            break;
        }
        case X64_OP_CVTI2F: {
            i64 v = (i64)(in->src_width == 8 ? s->val[in->a.r.v]
                                             : sim_sext(s->val[in->a.r.v], 4));

            s->val[in->def.v] =
                w == 8 ? sim_f64bits((double)v) : sim_f32bits((float)v);
            break;
        }
        case X64_OP_CVTF2F:
            if (w == 8)
                s->val[in->def.v] =
                    sim_f64bits((double)sim_f32(s->val[in->a.r.v]));
            else
                s->val[in->def.v] =
                    sim_f32bits((float)sim_f64(s->val[in->a.r.v]));
            break;
        case X64_OP_FXORM:
        case X64_OP_FANDM: {
            u64 m = sim_rdmem(t, s, sim_addr(t, s, &in->b.mem), 8);

            av = s->val[in->a.r.v];
            s->val[in->def.v] = in->op == X64_OP_FXORM ? (av ^ m) : (av & m);
            break;
        }
        case X64_OP_MOVQXR:
        case X64_OP_MOVQRX:
            s->val[in->def.v] =
                w == 4 ? sim_mask(s->val[in->a.r.v], 4) : s->val[in->a.r.v];
            break;
        /* --- x87 (host long double IS f80 on x86-64) ------------------- */
        case X64_OP_X87_FLD:
            T_ASSERT(t, s->nst < 8);
            s->st[s->nst++] = sim_ld_rd(t, s, sim_addr(t, s, &in->a.mem), w);
            break;
        case X64_OP_X87_FILD:
            T_ASSERT(t, s->nst < 8);
            s->st[s->nst++] = (long double)(i64)sim_rdmem(
                t, s, sim_addr(t, s, &in->a.mem), 8);
            break;
        case X64_OP_X87_FSTP:
            T_ASSERT(t, s->nst > 0);
            sim_ld_wr(t, s, sim_addr(t, s, &in->a.mem), s->st[--s->nst], w);
            break;
        case X64_OP_X87_FISTP: {
            long double v;
            i64 out;

            T_ASSERT(t, s->nst > 0);
            v = s->st[--s->nst];
            /* The RC field decides; our compiled sequences ALWAYS set
             * 11 = truncate before fistp (the dance) — a fistp under
             * any other mode is a bug the model refuses to guess at. */
            T_ASSERT(t, ((s->fcw >> 10) & 3) == 3);
            out = (i64)v;
            sim_wrmem(t, s, sim_addr(t, s, &in->a.mem), (u64)out, 8);
            break;
        }
        case X64_OP_X87_FADDP:
        case X64_OP_X87_FSUBP:
        case X64_OP_X87_FSUBRP:
        case X64_OP_X87_FMULP:
        case X64_OP_X87_FDIVP:
        case X64_OP_X87_FDIVRP: {
            long double s0, s1;

            T_ASSERT(t, s->nst >= 2);
            s0 = s->st[s->nst - 1];
            s1 = s->st[s->nst - 2];
            s->nst--;
            s->st[s->nst - 1] = in->op == X64_OP_X87_FADDP    ? s1 + s0
                                : in->op == X64_OP_X87_FSUBP  ? s1 - s0
                                : in->op == X64_OP_X87_FSUBRP ? s0 - s1
                                : in->op == X64_OP_X87_FMULP  ? s1 * s0
                                : in->op == X64_OP_X87_FDIVP  ? s1 / s0
                                                              : s0 / s1;
            break;
        }
        case X64_OP_X87_FCHS:
            T_ASSERT(t, s->nst >= 1);
            s->st[s->nst - 1] = -s->st[s->nst - 1];
            break;
        case X64_OP_X87_FABS:
            T_ASSERT(t, s->nst >= 1);
            if (s->st[s->nst - 1] < 0)
                s->st[s->nst - 1] = -s->st[s->nst - 1];
            break;
        case X64_OP_X87_FUCOMIP:
            T_ASSERT(t, s->nst >= 2);
            sim_fcmp_flags(s, s->st[s->nst - 1], s->st[s->nst - 2]);
            s->nst--;
            break;
        case X64_OP_X87_FPOP:
            T_ASSERT(t, s->nst >= 1);
            s->nst--;
            break;
        case X64_OP_X87_FNSTCW:
            sim_wrmem(t, s, sim_addr(t, s, &in->a.mem), s->fcw, 2);
            break;
        case X64_OP_X87_FLDCW:
            s->fcw = (u16)sim_rdmem(t, s, sim_addr(t, s, &in->a.mem), 2);
            break;
        default:
            return false;
        }
    }
    return false; /* step budget: runaway loop */
}

#endif
