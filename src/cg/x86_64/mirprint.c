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
    "mov", "movabs", "movzx",  "movsx", "lea",  "add", "sub",  "and",
    "or",  "xor",    "imul",   "neg",   "not",  "shl", "shr",  "sar",
    "cmp", "test",   "setcc",  "cqo",   "idiv", "div", "load", "store",
    "jmp", "jcc",    "jmptbl", "ret",   "ud2",
};

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
        buf_printf(out, "v%u", m->base.v);
    else
        buf_printf(out, "frame");
    if (m->index.v)
        buf_printf(out, "+v%u*%u", m->index.v, m->scale);
    if (m->disp)
        buf_printf(out, "%+d", m->disp);
    buf_printf(out, "]");
}

static void poper(Buf *out, const X64Func *f, const X64Operand *o)
{
    switch (o->kind) {
    case X64O_VREG:
        buf_printf(out, "v%u", o->r.v);
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

    buf_printf(out, "mir @%s (vregs=%u)\n", f->name, f->nvregs);
    for (bi = 0; bi < f->nblocks; bi++) {
        const X64Block *b = &f->blocks[bi];

        buf_printf(out, "bb%u:\n", bi + 1);
        for (i = 0; i < b->n; i++) {
            const X64Inst *in = &b->insts[i];

            buf_printf(out, "    ");
            if (in->def.v) {
                buf_printf(out, "v%u", in->def.v);
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
        }
    }
    return bad;
}
