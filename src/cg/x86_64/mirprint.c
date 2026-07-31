#include "cg/x86_64/mir.h"

#include <string.h>

/* Deterministic MIR printer: pure function of the X64Func, vregs as
 * v<N>, blocks as bb<N> (1-based, matching the IR block ids they mirror
 * plus appended splits). Golden `// MIR_CHECK:` fixtures grep this. */

static char wname(u8 w)
{
    return w == 1 ? 'b' : w == 2 ? 'w' : w == 4 ? 'l' : 'q';
}

static const char *const op_names[] = {
    "mov", "movabs", "movzx", "movsx",      "lea",       "add",          "sub",
    "and", "or",     "xor",   "imul",       "neg",       "not",          "shl",
    "shr", "sar",    "cmp",   "test",       "setcc",     "cqo",          "idiv",
    "div", "load",   "store", "jmp",        "jcc",       "jmptbl",       "ret",
    "ud2", "push",   "pop",   "alloca_dyn", "stacksave", "stackrestore",
};

_Static_assert(sizeof(op_names) / sizeof(op_names[0]) == X64_OP_COUNT,
               "op_names covers every X64Op");

/* Encoding-order names; post-RA operands print as registers. */
static const char *const reg_names[X64_REG_COUNT] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
};

const char *x64_reg_name(u8 reg)
{
    return reg < X64_REG_COUNT ? reg_names[reg] : "r?";
}

static void preg(Buf *out, const X64Func *f, X64VReg r)
{
    if (f->allocated && r.v >= 1 && r.v <= X64_REG_COUNT)
        buf_printf(out, "%s", reg_names[r.v - 1]);
    else
        buf_printf(out, "v%u", r.v);
}

static void pmem(Buf *out, const X64Func *f, const X64Mem *m)
{
    buf_printf(out, "[");
    if (m->rip_sym) {
        buf_printf(out, "rip @%s", f->m->syms[m->rip_sym - 1]);
        if (m->disp)
            buf_printf(out, "%+d", m->disp);
        buf_printf(out, "]");
        return;
    }
    if (m->base.v)
        preg(out, f, m->base);
    else
        buf_printf(out, "frame");
    if (m->index.v) {
        buf_printf(out, "+");
        preg(out, f, m->index);
        buf_printf(out, "*%u", m->scale);
    }
    if (m->disp)
        buf_printf(out, "%+d", m->disp);
    buf_printf(out, "]");
}

static void poper(Buf *out, const X64Func *f, const X64Operand *o)
{
    switch (o->kind) {
    case X64O_VREG:
        preg(out, f, o->r);
        break;
    case X64O_IMM:
        buf_printf(out, "$%lld", (long long)o->imm);
        break;
    case X64O_MEM:
        pmem(out, f, &o->mem);
        break;
    default:
        buf_printf(out, "?");
        break;
    }
    if (o->kind == X64O_VREG && o->fixed)
        buf_printf(out, ":%u", o->fixed - 1);
}

void x64_mir_print(const X64Func *f, Buf *out)
{
    u32 bi, i;

    if (f->allocated)
        buf_printf(out, "mir @%s (frame=%u spills=%u)\n", f->name,
                   f->frame_size, f->spill_slots);
    else
        buf_printf(out, "mir @%s (vregs=%u)\n", f->name, f->nvregs);
    for (bi = 0; bi < f->nblocks; bi++) {
        const X64Block *b = &f->blocks[bi];

        buf_printf(out, "bb%u:\n", bi + 1);
        for (i = 0; i < b->n; i++) {
            const X64Inst *in = &b->insts[i];

            buf_printf(out, "    ");
            if (in->def.v) {
                preg(out, f, in->def);
                if (in->def_fixed)
                    buf_printf(out, ":%u", in->def_fixed - 1);
                buf_printf(out, " = ");
            }
            buf_printf(out, "%s", op_names[in->op]);
            if (in->op == X64_OP_JCC || in->op == X64_OP_SETCC)
                buf_printf(out, ".%s", x64_cc_name(in->cc));
            if (in->op != X64_OP_JMP && in->op != X64_OP_JCC &&
                in->op != X64_OP_RET && in->op != X64_OP_UD2 &&
                in->op != X64_OP_JMPTBL)
                buf_printf(out, ".%c", wname(in->width));
            if (in->op == X64_OP_MOVZX || in->op == X64_OP_MOVSX)
                buf_printf(out, "%c", wname(in->src_width));
            if (in->a.kind) {
                buf_printf(out, " ");
                poper(out, f, &in->a);
            }
            if (in->b.kind) {
                buf_printf(out, ", ");
                poper(out, f, &in->b);
            }
            if (in->op == X64_OP_JMP || in->op == X64_OP_JCC) {
                buf_printf(out, "%sbb%u", in->a.kind ? ", " : " ", in->target);
                if (in->target2)
                    buf_printf(out, ", bb%u", in->target2);
            }
            if (in->op == X64_OP_JMPTBL)
                buf_printf(out, ", table%u", in->table);
            if (in->flags & X64IF_TWO_ADDR)
                buf_printf(out, "  ; 2addr");
            buf_printf(out, "\n");
        }
    }
    for (bi = 0; bi < f->ntables; bi++) {
        buf_printf(out, "table%u:", bi);
        for (i = 0; i < f->tables[bi].n; i++)
            buf_printf(out, " bb%u", f->tables[bi].targets[i]);
        buf_printf(out, "\n");
    }
}

/* --- the MIR verifier ------------------------------------------------------
 */

static const u16 two_addr_ops[] = {
    X64_OP_ADD, X64_OP_SUB, X64_OP_AND, X64_OP_OR,  X64_OP_XOR, X64_OP_IMUL,
    X64_OP_NEG, X64_OP_NOT, X64_OP_SHL, X64_OP_SHR, X64_OP_SAR,
};

int x64_mir_verify(const X64Func *f, DiagCtx *dc)
{
    int bad = 0;
    u32 bi, i, k;
    Span sp = {0};

    for (bi = 0; bi < f->nblocks; bi++) {
        const X64Block *b = &f->blocks[bi];

        for (i = 0; i < b->n; i++) {
            const X64Inst *in = &b->insts[i];

            /* Flags discipline: nothing defining flags may sit between
             * a consumer and its recorded producer. */
            if (in->flags & X64IF_USES_FLAGS) {
                if (in->flags_src >= i) {
                    diag_emit(dc, DIAG_ERROR, sp,
                              "mir verify @%s bb%u:%u: flags consumer "
                              "precedes its producer",
                              f->name, bi + 1, i);
                    bad++;
                } else {
                    for (k = in->flags_src + 1; k < i; k++)
                        if (b->insts[k].flags & X64IF_DEFS_FLAGS) {
                            diag_emit(dc, DIAG_ERROR, sp,
                                      "mir verify @%s bb%u:%u: '%s' "
                                      "clobbers flags between producer "
                                      "and consumer",
                                      f->name, bi + 1, k,
                                      op_names[b->insts[k].op]);
                            bad++;
                        }
                }
            }
            /* Two-address marking is structural on the opcode. */
            for (k = 0; k < sizeof(two_addr_ops) / sizeof(u16); k++)
                if (in->op == two_addr_ops[k] &&
                    !(in->flags & X64IF_TWO_ADDR)) {
                    diag_emit(dc, DIAG_ERROR, sp,
                              "mir verify @%s bb%u:%u: '%s' missing the "
                              "two-address mark",
                              f->name, bi + 1, i, op_names[in->op]);
                    bad++;
                }
            /* Fold legality on every mem operand. */
            {
                const X64Operand *ops[2] = {&in->a, &in->b};
                u32 oi;

                for (oi = 0; oi < 2; oi++) {
                    const X64Mem *m = &ops[oi]->mem;

                    if (ops[oi]->kind != X64O_MEM)
                        continue;
                    if (m->rip_sym && (m->base.v || m->index.v)) {
                        diag_emit(dc, DIAG_ERROR, sp,
                                  "mir verify @%s bb%u:%u: rip-relative "
                                  "mem with base/index",
                                  f->name, bi + 1, i);
                        bad++;
                    }
                    if (m->index.v && !x64_fold_ok(m->scale, false, m->disp)) {
                        diag_emit(dc, DIAG_ERROR, sp,
                                  "mir verify @%s bb%u:%u: illegal "
                                  "addressing fold (scale %u)",
                                  f->name, bi + 1, i, m->scale);
                        bad++;
                    }
                }
            }
            /* imm64 discipline: only movabs carries one. A 32-bit-or-
             * narrower op takes any imm32 bit pattern (mov.l $0x80000000
             * is legal and zero-extends); Q-width ops SIGN-extend, so
             * only simm32 fits there. */
            if (in->op != X64_OP_MOVABS && in->a.kind == X64O_IMM &&
                (in->width == X64_Q
                     ? !x64_imm_fits_simm32(in->a.imm)
                     : in->a.imm < -2147483648ll || in->a.imm > 4294967295ll)) {
                diag_emit(dc, DIAG_ERROR, sp,
                          "mir verify @%s bb%u:%u: imm64 outside movabs",
                          f->name, bi + 1, i);
                bad++;
            }
            if (in->b.kind == X64O_IMM &&
                (in->width == X64_Q
                     ? !x64_imm_fits_simm32(in->b.imm)
                     : in->b.imm < -2147483648ll || in->b.imm > 4294967295ll)) {
                diag_emit(dc, DIAG_ERROR, sp,
                          "mir verify @%s bb%u:%u: imm64 outside movabs",
                          f->name, bi + 1, i);
                bad++;
            }
            /* Allocation-state discipline. Pre-RA: frame code (push/pop)
             * cannot exist yet. Post-RA: no vreg survives (every id is a
             * register), no constraint annotation survives, no marker op
             * survives, and every TWO_ADDR inst is in the x86 canonical
             * form def == a. */
            if (!f->allocated &&
                (in->op == X64_OP_PUSH || in->op == X64_OP_POP)) {
                diag_emit(dc, DIAG_ERROR, sp,
                          "mir verify @%s bb%u:%u: '%s' before allocation",
                          f->name, bi + 1, i, op_names[in->op]);
                bad++;
            }
            if (f->allocated) {
                X64VReg regs[6];
                u32 nr = 0, ri;

                if (in->def.v)
                    regs[nr++] = in->def;
                if (in->a.kind == X64O_VREG)
                    regs[nr++] = in->a.r;
                if (in->b.kind == X64O_VREG)
                    regs[nr++] = in->b.r;
                if (in->a.kind == X64O_MEM) {
                    if (in->a.mem.base.v)
                        regs[nr++] = in->a.mem.base;
                    if (in->a.mem.index.v)
                        regs[nr++] = in->a.mem.index;
                }
                if (in->b.kind == X64O_MEM) {
                    if (in->b.mem.base.v)
                        regs[nr++] = in->b.mem.base;
                    if (in->b.mem.index.v)
                        regs[nr++] = in->b.mem.index;
                }
                for (ri = 0; ri < nr; ri++)
                    if (regs[ri].v < 1 || regs[ri].v > X64_REG_COUNT) {
                        diag_emit(dc, DIAG_ERROR, sp,
                                  "mir verify @%s bb%u:%u: virtual "
                                  "register v%u survived allocation",
                                  f->name, bi + 1, i, regs[ri].v);
                        bad++;
                    }
                if (in->def_fixed || in->a.fixed || in->b.fixed || in->xuse.v ||
                    in->xuse_fixed) {
                    diag_emit(dc, DIAG_ERROR, sp,
                              "mir verify @%s bb%u:%u: constraint "
                              "annotation survived allocation",
                              f->name, bi + 1, i);
                    bad++;
                }
                if (in->op == X64_OP_ALLOCA_DYN || in->op == X64_OP_STACKSAVE ||
                    in->op == X64_OP_STACKRESTORE) {
                    diag_emit(dc, DIAG_ERROR, sp,
                              "mir verify @%s bb%u:%u: '%s' marker "
                              "survived allocation",
                              f->name, bi + 1, i, op_names[in->op]);
                    bad++;
                }
                if ((in->flags & X64IF_TWO_ADDR) &&
                    (in->a.kind != X64O_VREG || in->def.v != in->a.r.v)) {
                    diag_emit(dc, DIAG_ERROR, sp,
                              "mir verify @%s bb%u:%u: two-address '%s' "
                              "not in def == src1 form",
                              f->name, bi + 1, i, op_names[in->op]);
                    bad++;
                }
            }
        }
    }
    return bad;
}
