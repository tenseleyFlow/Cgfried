#include "cg/arm64/debug.h"

#include <string.h>

#include "cg/debug.h"

/* AArch64 .eh_frame, plus the row extraction the target-neutral line table
 * consumes. See cg/debug.h for why the line table itself is not here.
 *
 * THE CONTRAST WITH x86 IS THE WHOLE DIFFICULTY. x86's FDE program is the
 * same sixteen bytes for every function, because the prologue is always
 * `push %rbp; mov %rsp,%rbp` and the CFA moves by a constant 16. AArch64's
 * prologue allocates the WHOLE frame in its first instruction, so the FDE
 * carries the frame size and no two functions share a program.
 *
 * The canonical shape (see frame_emit_prologue):
 *
 *     stp x29, x30, [sp, #-TOTAL]!   // one instruction: allocate and save
 *     add x29, sp, #0                // the MOV-from-SP alias
 *
 * or, when outgoing stack arguments own SP+0 and the pair moves up:
 *
 *     sub sp, sp, #TOTAL             // one or two instructions
 *     stp x29, x30, [sp, #BASE]
 *     add x29, sp, #BASE
 *
 * A BASE beyond the pair instruction's +504 limit adds one address step:
 *
 *     sub sp, sp, #TOTAL
 *     add x16, sp, #BASE             // one or two instructions
 *     stp x29, x30, [x16]
 *     add x29, x16, #0
 *
 * The pre-indexed form changes the CFA and saves both registers in ONE
 * instruction, so all three rules take effect at the same advance; the
 * separate form needs the CFA change before any address-only setup. The
 * prologue records both instruction counts rather than this file re-deriving
 * which branch ran. */

/* DWARF register numbers, DWARF for the ARM 64-bit Architecture 4.1:
 * x0-x30 are 0-30, SP is 31. The return address lives in x30. */
enum { A64_DWREG_FP = 29, A64_DWREG_LR = 30, A64_DWREG_SP = 31 };

static void emit_byte_list(Buf *out, const u8 *bytes, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        buf_printf(out, "%s%u", i % 12 == 0 ? "\t.byte\t" : ",",
                   (unsigned)bytes[i]);
    if (n)
        buf_printf(out, "\n");
}

static void emit_uleb(Buf *out, u64 value)
{
    u8 tmp[10];
    size_t n = cg_dwarf_uleb(value, tmp);

    emit_byte_list(out, tmp, n);
}

void a64_debug_prepare(A64Func *f)
{
    u32 bi, i, next = 0, previous = 0;

    f->debug_lines = true;
    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];

        for (i = 0; i < b->n; i++) {
            A64Inst *in = &b->insts[i];

            in->debug_label = 0;
            if (in->loc != previous) {
                if (in->loc || previous)
                    in->debug_label = ++next;
                previous = in->loc;
            }
        }
    }
}

/* The CIE differs from x86's in three fields, and every one of them would
 * silently corrupt an unwind if copied over: the code alignment factor is 4
 * because every A64 instruction is four bytes (x86 uses 1), the return
 * address register is x30 rather than x86's synthetic 16, and the initial
 * CFA rule names SP as register 31. The data alignment factor -8 is shared,
 * being a property of the pointer size. */
static void emit_cie(Buf *out)
{
    static const u8 cie[] = {
        1,
        'z',
        'R',
        0,            /* version 1, augmentation "zR" */
        4,            /* code_alignment_factor: 4-byte instructions */
        0x78,         /* data_alignment_factor: -8, sleb */
        A64_DWREG_LR, /* return_address_register: x30 */
        1,
        0x1b, /* augmentation data: DW_EH_PE_pcrel|sdata4 */
        0x0c,
        A64_DWREG_SP,
        0 /* DW_CFA_def_cfa: sp, offset 0 */
    };

    buf_printf(out, "\t.section\t.eh_frame,\"a\",@progbits\n"
                    "\t.p2align\t3\n"
                    ".Lcie0:\n"
                    "\t.long\t.Lcie_end-.Lcie_start\n"
                    ".Lcie_start:\n"
                    "\t.long\t0\n");
    emit_byte_list(out, cie, sizeof(cie));
    /* .p2align pads with zero, which is DW_CFA_nop -- the CIE's length field
     * already covers the padding, so no explicit nop bytes are needed. */
    buf_printf(out, "\t.p2align\t3\n.Lcie_end:\n");
}

/* DW_CFA_offset(reg, n): the register is saved at CFA + n * data_align, and
 * data_align is -8, so `n` counts eightbytes BELOW the CFA. The pair sits at
 * new_sp + pair_off, and the CFA is new_sp + frame, so the distance below the
 * CFA is frame - pair_off. */
static void emit_saved_pair(Buf *out, u32 frame, u32 pair_off)
{
    u32 fp_below = frame - pair_off;

    if (pair_off > frame || fp_below < 8 || (fp_below & 7u))
        CGF_ICE("a64 CFI: saved pair at %u in a %u-byte frame is not a "
                "sane eightbyte position",
                pair_off, frame);
    buf_printf(out, "\t.byte\t%u\n", 0x80u | A64_DWREG_FP);
    emit_uleb(out, fp_below / 8u);
    buf_printf(out, "\t.byte\t%u\n", 0x80u | A64_DWREG_LR);
    emit_uleb(out, (fp_below - 8u) / 8u);
}

static void emit_advance_label(Buf *out, u32 fidx, u32 label, u32 previous,
                               u32 prior_insns)
{
    /* DW_CFA_advance_loc4's operand is measured in code-alignment units.
     * A64-M-04: label differences, rather than MIR instruction counts, keep
     * the unwind program correct across arbitrary-length inline asm. */
    buf_printf(out, "\t.byte\t4\n\t.long\t(.Lcfi_%u_%u-", fidx, label);
    if (previous)
        buf_printf(out, ".Lcfi_%u_%u)/4\n", fidx, previous);
    else
        buf_printf(out, ".Lfb%u)/4-%u\n", fidx, prior_insns);
}

static void emit_offset_rule(Buf *out, u32 dwarf_reg, u32 byte_below_cfa)
{
    if (!byte_below_cfa || (byte_below_cfa & 7u))
        CGF_ICE("a64 CFI: register %u has invalid CFA distance %u", dwarf_reg,
                byte_below_cfa);
    if (dwarf_reg < 64) {
        buf_printf(out, "\t.byte\t%u\n", 0x80u | dwarf_reg);
    } else {
        buf_printf(out, "\t.byte\t5\n"); /* DW_CFA_offset_extended */
        emit_uleb(out, dwarf_reg);
    }
    emit_uleb(out, byte_below_cfa / 8u);
}

static void emit_restore_rule(Buf *out, u32 dwarf_reg)
{
    if (dwarf_reg < 64)
        buf_printf(out, "\t.byte\t%u\n", 0xc0u | dwarf_reg);
    else {
        buf_printf(out, "\t.byte\t6\n"); /* DW_CFA_restore_extended */
        emit_uleb(out, dwarf_reg);
    }
}

static void emit_saved_callee_regs(Buf *out, const A64Func *f)
{
    u32 i, off = f->cfi_pair_off + 16u;

    for (i = 0; i < f->cfi_ngp; i++, off += 8u) {
        if (off >= f->cfi_frame)
            CGF_ICE("a64 CFI: x%u save at %u escapes %u-byte frame",
                    f->cfi_gp[i], off, f->cfi_frame);
        emit_offset_rule(out, f->cfi_gp[i], f->cfi_frame - off);
    }
    for (i = 0; i < f->cfi_nfp; i++, off += 8u) {
        if (off >= f->cfi_frame)
            CGF_ICE("a64 CFI: d%u save at %u escapes %u-byte frame",
                    (u32)(f->cfi_fp[i] - A64_V0), off, f->cfi_frame);
        emit_offset_rule(out, 64u + (u32)(f->cfi_fp[i] - A64_V0),
                         f->cfi_frame - off);
    }
}

static void emit_restored_callee_regs(Buf *out, const A64Func *f)
{
    u32 i;

    for (i = 0; i < f->cfi_ngp; i++)
        emit_restore_rule(out, f->cfi_gp[i]);
    for (i = 0; i < f->cfi_nfp; i++)
        emit_restore_rule(out, 64u + (u32)(f->cfi_fp[i] - A64_V0));
}

static void emit_fde(Buf *out, const A64Func *f, u32 idx)
{
    u32 i, previous, prior_insns;

    buf_printf(out,
               ".Lfde%u:\n"
               "\t.long\t.Lfde%u_end-.Lfde%u_start\n"
               ".Lfde%u_start:\n"
               ".Lfde%u_cie:\n"
               "\t.long\t.Lfde%u_cie-.Lcie0\n"
               "\t.long\t.Lfb%u-.\n"
               "\t.long\t.Lfe%u_0-.Lfb%u\n"
               "\t.byte\t0\n", /* augmentation data length */
               idx, idx, idx, idx, idx, idx, idx, idx, idx);
    if (!f->cfi_frame) {
        /* A leaf with no frame at all: nothing was pushed, so the entry
         * rule from the CIE already describes every instruction. */
        buf_printf(out, "\t.p2align\t3\n.Lfde%u_end:\n", idx);
        return;
    }
    if (f->cfi_pre_insns) {
        /* Each SUB changes the live CFA immediately. Collapsing a two-part
         * 4096+remainder adjustment into one final row leaves asynchronous
         * unwinders with CFA=sp between the instructions. */
        for (i = 0; i < f->cfi_pre_insns; i++) {
            if (!f->cfi_sp_offsets[i] ||
                (i && f->cfi_sp_offsets[i] <= f->cfi_sp_offsets[i - 1]))
                CGF_ICE("a64 CFI: invalid cumulative SP adjustment %u",
                        f->cfi_sp_offsets[i]);
            buf_printf(out, "\t.byte\t65\n"); /* advance_loc 1: sub */
            buf_printf(out, "\t.byte\t14\n"); /* def_cfa_offset */
            emit_uleb(out, f->cfi_sp_offsets[i]);
        }
        if (f->cfi_sp_offsets[f->cfi_pre_insns - 1] != f->cfi_frame)
            CGF_ICE("a64 CFI: SP rows cover %u of %u frame bytes",
                    f->cfi_sp_offsets[f->cfi_pre_insns - 1], f->cfi_frame);
        if (f->cfi_pair_pre_insns)
            buf_printf(out, "\t.byte\t%u\n", 0x40u | f->cfi_pair_pre_insns);
        buf_printf(out, "\t.byte\t65\n"); /* advance_loc 1: the stp */
        emit_saved_pair(out, f->cfi_frame, f->cfi_pair_off);
    } else {
        /* The pre-indexed stp did both in one instruction. */
        buf_printf(out, "\t.byte\t65\n"); /* advance_loc 1 */
        buf_printf(out, "\t.byte\t14\n"); /* DW_CFA_def_cfa_offset */
        emit_uleb(out, f->cfi_frame);
        emit_saved_pair(out, f->cfi_frame, f->cfi_pair_off);
    }
    /* add x29, sp, #BASE makes the frame pointer the CFA base, which is what
     * lets an unwinder walk a frame whose SP has since moved -- a VLA, or an
     * outgoing-argument area built after the prologue. */
    buf_printf(out, "\t.byte\t65\n"); /* advance_loc 1: the add */
    buf_printf(out, "\t.byte\t13,%u\n", A64_DWREG_FP); /* def_cfa_register */
    if (f->cfi_pair_off) {
        buf_printf(out, "\t.byte\t14\n"); /* def_cfa_offset from x29 */
        emit_uleb(out, f->cfi_frame - f->cfi_pair_off);
    }
    /* A64-M-04: callee-saved registers become unwind-visible before body
     * code may repurpose them, and every return path restores both register
     * and CFA rules at its exact instruction boundaries. */
    prior_insns =
        f->cfi_pre_insns ? f->cfi_pre_insns + f->cfi_pair_pre_insns + 2u : 2u;
    previous = 0;
    if (f->cfi_body_label) {
        emit_advance_label(out, idx, f->cfi_body_label, 0, prior_insns);
        emit_saved_callee_regs(out, f);
        previous = f->cfi_body_label;
    }
    for (i = 0; i < f->cfi_nepilogues; i++) {
        const A64CfiEpilogue *ep = &f->cfi_epilogues[i];

        emit_advance_label(out, idx, ep->before_pair, previous, 0);
        buf_printf(out, "\t.byte\t10\n"); /* DW_CFA_remember_state */
        emit_restored_callee_regs(out, f);
        buf_printf(out, "\t.byte\t12\n"); /* DW_CFA_def_cfa */
        emit_uleb(out, A64_DWREG_SP);
        emit_uleb(out, f->cfi_frame);
        emit_advance_label(out, idx, ep->after_pair, ep->before_pair, 0);
        emit_restore_rule(out, A64_DWREG_FP);
        emit_restore_rule(out, A64_DWREG_LR);
        if (!ep->nsp) {
            buf_printf(out, "\t.byte\t14\n"); /* def_cfa_offset */
            emit_uleb(out, 0);
        } else {
            u32 j, last = ep->after_pair;

            /* A64-M-04: a large epilogue restores SP in several ADDs. Each
             * architectural boundary needs its cumulative remaining CFA
             * offset, including the intermediate nonzero rows. */
            for (j = 0; j < ep->nsp; j++) {
                if ((j && ep->sp_offsets[j] >= ep->sp_offsets[j - 1]) ||
                    (j + 1 == ep->nsp && ep->sp_offsets[j] != 0))
                    CGF_ICE("a64 CFI: invalid epilogue CFA offset %u",
                            ep->sp_offsets[j]);
                emit_advance_label(out, idx, ep->sp_labels[j], last, 0);
                buf_printf(out, "\t.byte\t14\n"); /* def_cfa_offset */
                emit_uleb(out, ep->sp_offsets[j]);
                last = ep->sp_labels[j];
            }
        }
        emit_advance_label(
            out, idx, ep->after_ret,
            ep->nsp ? ep->sp_labels[ep->nsp - 1] : ep->after_pair, 0);
        buf_printf(out, "\t.byte\t11\n"); /* DW_CFA_restore_state */
        previous = ep->after_ret;
    }
    buf_printf(out, "\t.p2align\t3\n.Lfde%u_end:\n", idx);
}

void a64_emit_debug_sections(TargetSpec target, Arena *arena, const IrModule *m,
                             A64Func *const *funcs, u32 nfuncs,
                             const char *input, const char *comp_dir,
                             bool emit_debug, Buf *out)
{
    CgDebugFunc *dfuncs;
    u32 fi, bi, i, n;

    if (target.kind != CGF_TARGET_ARM64_LINUX)
        CGF_ICE("a64 CFI encoder received target %d; Mach-O uses compact "
                "unwind rather than .eh_frame",
                (int)target.kind);
    emit_cie(out);
    for (fi = 0; fi < nfuncs; fi++)
        emit_fde(out, funcs[fi], fi);
    if (!emit_debug)
        return;

    dfuncs = arena_alloc(arena, (nfuncs ? nfuncs : 1) * sizeof(*dfuncs),
                         _Alignof(CgDebugFunc));
    for (fi = 0; fi < nfuncs; fi++) {
        CgDebugRow *rows;

        n = 0;
        for (bi = 0; bi < funcs[fi]->nblocks; bi++)
            n += funcs[fi]->blocks[bi].n;
        rows = arena_alloc(arena, (n ? n : 1) * sizeof(*rows),
                           _Alignof(CgDebugRow));
        n = 0;
        for (bi = 0; bi < funcs[fi]->nblocks; bi++) {
            const A64Block *b = &funcs[fi]->blocks[bi];

            for (i = 0; i < b->n; i++)
                if (b->insts[i].debug_label) {
                    rows[n].label = b->insts[i].debug_label;
                    rows[n].loc = b->insts[i].loc;
                    n++;
                }
        }
        dfuncs[fi].rows = rows;
        dfuncs[fi].nrows = n;
    }
    cg_emit_debug_info(arena, m, dfuncs, nfuncs, input, comp_dir, out);
}
