#include "cg/arm64/mir.h"

#include <string.h>

/* FCMP unordered is NZCV=0011.  Consequently ordered < is MI (not LT),
 * ordered <= is LS, and unordered is VS.  Predicates not expressible by one
 * condition explicitly carry their second test. */
const A64FpCondMap a64_fp_cond_map[A64_FP_PRED_COUNT] = {
    [A64_FP_OEQ] = {A64_CC_EQ, A64_CC_AL, false},
    [A64_FP_ONE] = {A64_CC_NE, A64_CC_VC, false},
    [A64_FP_OLT] = {A64_CC_MI, A64_CC_AL, false},
    [A64_FP_OLE] = {A64_CC_LS, A64_CC_AL, false},
    [A64_FP_OGT] = {A64_CC_GT, A64_CC_AL, false},
    [A64_FP_OGE] = {A64_CC_GE, A64_CC_AL, false},
    [A64_FP_UEQ] = {A64_CC_EQ, A64_CC_VS, true},
    [A64_FP_UNE] = {A64_CC_NE, A64_CC_AL, false},
    [A64_FP_ULT] = {A64_CC_LT, A64_CC_AL, false},
    [A64_FP_ULE] = {A64_CC_LE, A64_CC_AL, false},
    [A64_FP_UGT] = {A64_CC_HI, A64_CC_AL, false},
    [A64_FP_UGE] = {A64_CC_PL, A64_CC_AL, false},
    [A64_FP_ORD] = {A64_CC_VC, A64_CC_AL, false},
    [A64_FP_UNO] = {A64_CC_VS, A64_CC_AL, false},
};

A64Reg a64_phys(A64PhysReg reg)
{
    A64Reg r = {(u32)reg + 1, 1};
    return r;
}

u8 a64_phys_encode(A64PhysReg reg)
{
    if (reg == A64_SP || reg == A64_XZR)
        return 31;
    if (reg <= A64_X30)
        return (u8)reg;
    if (reg >= A64_V0 && reg <= A64_V31)
        return (u8)(reg - A64_V0);
    return 255;
}

/* Deduped so that the same literal, and above all the single sign mask every
 * fneg in a function shares, occupies one slot. The scan is linear because a
 * function's pool is a handful of entries; a hash here would cost more in
 * code than it saves. */
u32 a64_cpool_add(A64Func *f, u64 lo, u64 hi)
{
    u32 i;

    for (i = 0; i < f->ncpool; i++)
        if (f->cpool[2 * i] == lo && f->cpool[2 * i + 1] == hi)
            return i;
    if (f->ncpool == f->cap_cpool) {
        u32 nc = f->cap_cpool ? f->cap_cpool * 2 : 4;
        u64 *nv = arena_alloc(f->arena, nc * 2 * sizeof(*nv), _Alignof(u64));

        if (f->cap_cpool)
            memcpy(nv, f->cpool, f->cap_cpool * 2 * sizeof(*nv));
        f->cpool = nv;
        f->cap_cpool = nc;
    }
    f->cpool[2 * f->ncpool] = lo;
    f->cpool[2 * f->ncpool + 1] = hi;
    return f->ncpool++;
}

A64Reg a64_newv(A64Func *f, A64RegClass rc)
{
    return a64_newv_width(f, rc, A64_SF64);
}

A64Reg a64_newv_width(A64Func *f, A64RegClass rc, A64Sf sf)
{
    A64Reg r = {++f->nvregs, 0};

    if (f->nvregs + 1 > f->cap_vclass) {
        u32 nc = f->cap_vclass ? f->cap_vclass * 2 : 64;
        u8 *classes;
        u8 *widths;
        u8 *fixed;

        while (nc < f->nvregs + 1)
            nc *= 2;
        classes = arena_alloc(f->arena, nc, 1);
        widths = arena_alloc(f->arena, nc, 1);
        fixed = arena_alloc(f->arena, nc, 1);
        if (f->cap_vclass) {
            memcpy(classes, f->vclass, f->cap_vclass);
            memcpy(widths, f->vwidth, f->cap_vclass);
            memcpy(fixed, f->vfixed, f->cap_vclass);
        }
        memset(classes + f->cap_vclass, 0, nc - f->cap_vclass);
        memset(widths + f->cap_vclass, 0, nc - f->cap_vclass);
        memset(fixed + f->cap_vclass, 0, nc - f->cap_vclass);
        f->vclass = classes;
        f->vwidth = widths;
        f->vfixed = fixed;
        f->cap_vclass = nc;
    }
    f->vclass[r.id] = (u8)rc;
    f->vwidth[r.id] = (u8)sf;
    return r;
}

/* A virtual register the allocator MUST colour `phys`. Argument marshalling
 * and parameter binding are the only producers; everything else allocates
 * freely. */
A64Reg a64_newv_fixed(A64Func *f, A64RegClass rc, A64Sf sf, u8 phys)
{
    A64Reg r = a64_newv_width(f, rc, sf);

    f->vfixed[r.id] = (u8)(phys + 1);
    return r;
}

u8 a64_vclass(const A64Func *f, A64Reg reg)
{
    if (reg.physical) {
        u32 phys = reg.id - 1;

        if (phys >= A64_V0 && phys <= A64_V31)
            return A64RC_FP;
        if (phys == A64_NZCV)
            return A64RC_NZCV;
        return A64RC_GP;
    }
    if (!reg.id || reg.id > f->nvregs || !f->vclass)
        return A64RC_GP;
    return f->vclass[reg.id];
}

u8 a64_vwidth(const A64Func *f, A64Reg reg)
{
    if (reg.physical || !reg.id || reg.id > f->nvregs || !f->vwidth)
        return A64_SF64;
    return f->vwidth[reg.id];
}

void a64_block_append(A64Func *f, A64Block *b, A64Inst inst)
{
    if (b->n == b->cap) {
        u32 nc = b->cap ? b->cap * 2 : 16;
        A64Inst *nv =
            arena_alloc(f->arena, nc * sizeof(*nv), _Alignof(A64Inst));

        if (b->n)
            memcpy(nv, b->insts, b->n * sizeof(*nv));
        b->insts = nv;
        b->cap = nc;
    }
    b->insts[b->n++] = inst;
}

A64CallInfo *a64_call_info_new(A64Func *f, A64Inst *inst, u8 callee_ref_kind,
                               u32 callee_id, A64Reg indirect, A64Reg result,
                               u8 result_type, u8 abi_ret, bool variadic,
                               bool noreturn)
{
    A64CallInfo *call =
        arena_alloc(f->arena, sizeof(*call), _Alignof(A64CallInfo));

    *call = (A64CallInfo){0};
    call->callee_ref_kind = callee_ref_kind;
    call->callee_id = callee_id;
    call->indirect = indirect;
    call->result = result;
    call->result_type = result_type;
    call->abi_ret = abi_ret;
    call->variadic = variadic;
    call->noreturn = noreturn;
    inst->call = call;
    return call;
}

void a64_call_add_arg(A64Func *f, A64CallInfo *call, A64Reg value, u8 type,
                      u8 argflags, u64 abi_annot)
{
    A64CallArg *args = arena_alloc(f->arena, (call->nargs + 1) * sizeof(*args),
                                   _Alignof(A64CallArg));

    if (call->nargs)
        memcpy(args, call->args, call->nargs * sizeof(*args));
    args[call->nargs] = (A64CallArg){value, type, argflags, abi_annot};
    call->args = args;
    call->nargs++;
}

static u64 low_mask(unsigned bits)
{
    return bits == 64 ? ~(u64)0 : (((u64)1 << bits) - 1);
}

static u64 ror_width(u64 value, unsigned rotate, unsigned width)
{
    u64 mask = low_mask(width);

    rotate &= width - 1;
    value &= mask;
    if (!rotate)
        return value;
    return ((value >> rotate) | (value << (width - rotate))) & mask;
}

static u64 replicate(u64 elem, unsigned esize, unsigned width)
{
    u64 out = 0;
    unsigned bit;

    for (bit = 0; bit < width; bit += esize)
        out |= elem << bit;
    return out & low_mask(width);
}

bool a64_logical_imm_encode(u64 value, unsigned width, u32 *packed)
{
    unsigned esize;

    if ((width != 32 && width != 64) || (width == 32 && value >> 32))
        return false;
    value &= low_mask(width);
    if (!value || value == low_mask(width))
        return false;

    for (esize = 2; esize <= width; esize *= 2) {
        u64 elem = value & low_mask(esize);
        unsigned ones;

        if (replicate(elem, esize, width) != value)
            continue;
        for (ones = 1; ones < esize; ones++) {
            u64 run = low_mask(ones);
            unsigned rotate;

            for (rotate = 0; rotate < esize; rotate++) {
                unsigned imms;

                if (ror_width(run, rotate, esize) != elem)
                    continue;
                /* N:immr:imms, exactly in instruction bit-field order. */
                imms = ((unsigned)(-(int)(2 * esize)) | (ones - 1)) & 63;
                if (packed)
                    *packed =
                        (esize == 64 ? 1u << 12 : 0) | (rotate << 6) | imms;
                return true;
            }
        }
    }
    return false;
}

u32 a64_synth_mov_width(u64 value, unsigned width, A64MovSynth out[4])
{
    u32 logical;
    u16 hw[4];
    unsigned halfwords, zeros = 0, ones = 0, i, seed;
    bool use_movn;
    u32 n = 0;

    if (width != 32 && width != 64)
        CGF_ICE("arm64 mov synthesis: invalid width %u", width);
    value &= low_mask(width);
    halfwords = width / 16;
    if (a64_logical_imm_encode(value, width, &logical)) {
        out[0] = (A64MovSynth){A64_MOV_ORR, 0, 0, logical};
        return 1;
    }
    for (i = 0; i < halfwords; i++) {
        hw[i] = (u16)(value >> (i * 16));
        zeros += hw[i] == 0;
        ones += hw[i] == 0xffff;
    }
    use_movn = ones > zeros;
    seed = 0;
    while (seed < halfwords && hw[seed] == (use_movn ? 0xffffu : 0u))
        seed++;
    if (seed == halfwords) {
        out[0] = (A64MovSynth){use_movn ? A64_MOV_MOVN : A64_MOV_MOVZ, 0, 0, 0};
        return 1;
    }
    out[n++] =
        (A64MovSynth){use_movn ? A64_MOV_MOVN : A64_MOV_MOVZ,
                      use_movn ? (u16)~hw[seed] : hw[seed], (u8)(seed * 16), 0};
    for (i = 0; i < halfwords; i++) {
        if (i == seed || hw[i] == (use_movn ? 0xffffu : 0u))
            continue;
        out[n++] = (A64MovSynth){A64_MOV_MOVK, hw[i], (u8)(i * 16), 0};
    }
    return n;
}

u32 a64_synth_mov(u64 value, A64MovSynth out[4])
{
    return a64_synth_mov_width(value, 64, out);
}

bool a64_addsub_imm(i64 value, A64AddSubImm *out)
{
    bool sub = value < 0;
    u64 magnitude = sub ? (u64)(-(value + 1)) + 1 : (u64)value;
    u8 shift;
    u64 imm;

    if (magnitude <= 4095) {
        shift = 0;
        imm = magnitude;
    } else if (!(magnitude & 0xfff) && (magnitude >> 12) <= 4095) {
        shift = 12;
        imm = magnitude >> 12;
    } else {
        return false;
    }
    if (out)
        *out = (A64AddSubImm){sub, (u16)imm, shift};
    return true;
}

static u64 fp_expand_imm(u8 imm8, unsigned width)
{
    unsigned ebits = width == 32 ? 8 : 11;
    unsigned fbits = width - ebits - 1;
    unsigned b = (imm8 >> 6) & 1;
    u64 exp = (u64)!b << (ebits - 1);
    unsigned i;

    for (i = 2; i <= ebits - 2; i++)
        exp |= (u64)b << i;
    exp |= (imm8 >> 4) & 3;
    return ((u64)(imm8 >> 7) << (width - 1)) | (exp << fbits) |
           ((u64)(imm8 & 15) << (fbits - 4));
}

bool a64_fp_imm_encode(u64 bits, unsigned width, u8 *imm8)
{
    unsigned i;

    if ((width != 32 && width != 64) || (width == 32 && bits >> 32))
        return false;
    for (i = 0; i < 256; i++)
        if (fp_expand_imm((u8)i, width) == bits) {
            if (imm8)
                *imm8 = (u8)i;
            return true;
        }
    return false;
}

static bool valid_mem_size(u8 size)
{
    return size == 1 || size == 2 || size == 4 || size == 8 || size == 16;
}

A64AddrMode a64_isel_addr(i64 offset, u8 size, bool pre, bool post)
{
    if (!valid_mem_size(size) || (pre && post))
        return A64_ADDR_MATERIALIZE;
    if (pre || post) {
        if (offset < -256 || offset > 255)
            return A64_ADDR_MATERIALIZE;
        return pre ? A64_ADDR_PRE : A64_ADDR_POST;
    }
    if (offset >= 0 && (u64)offset <= (u64)4095 * size &&
        (offset & (size - 1)) == 0)
        return A64_ADDR_SCALED;
    if (offset >= -256 && offset <= 255)
        return A64_ADDR_UNSCALED;
    return A64_ADDR_MATERIALIZE;
}

A64AddrMode a64_addr_reg_mode(bool index32, bool index_signed, bool scaled)
{
    if (!index32)
        return A64_ADDR_REG_LSL;
    (void)scaled; /* shift=0 and shift=log2(size) use the same extend mode. */
    return index_signed ? A64_ADDR_REG_SXTW : A64_ADDR_REG_UXTW;
}
