#include "cg/x86_64/debug.h"

#include "cg/debug.h"

#include <string.h>

/* DWARF v4 line tables + the minimal CU DIE gdb needs, and x86-64
 * .eh_frame CFI. .debug_frame would serve debuggers only; .eh_frame serves
 * both debuggers and runtime unwinders, so it ships even without -g.
 *
 * The x86 FDE program relies on the v0.1.0 always-rbp prologue:
 *   push %rbp                 (1 byte)
 *   mov  %rsp, %rbp           (3 bytes)
 * -fomit-frame-pointer is therefore warn+ignored until a future CFI design.
 * Epilogue windows are deliberately not described in v0.1.0; ordinary C
 * backtraces sample call sites, where the rule is exact. */

void x64_debug_prepare(X64Func *f)
{
    u32 bi, i, next = 0, previous = 0;

    f->debug_lines = true;
    for (bi = 0; bi < f->nblocks; bi++) {
        X64Block *b = &f->blocks[bi];

        for (i = 0; i < b->n; i++) {
            X64Inst *in = &b->insts[i];

            in->debug_label = 0;
            if (in->loc != previous) {
                if (in->loc || previous)
                    in->debug_label = ++next;
                previous = in->loc;
            }
        }
    }
}

static void emit_byte_list(Buf *out, const u8 *bytes, size_t n)
{
    size_t i;

    if (!n)
        return;
    buf_printf(out, "\t.byte\t");
    for (i = 0; i < n; i++)
        buf_printf(out, "%s%u", i ? "," : "", (u32)bytes[i]);
    buf_printf(out, "\n");
}

static void emit_eh_frame(TargetSpec target, const IrModule *m, u32 nfuncs,
                          Buf *out)
{
    static const u8 cie[] = {1,    'z',  'R', 0, 1,    0x78, 16, 1,
                             0x1b, 0x0c, 7,   8, 0x90, 1,    0,  0};
    static const u8 fde[] = {0,    0x41, 0x0e, 0x10, 0x86, 0x02, 0x43, 0x0d,
                             0x06, 0,    0,    0,    0,    0,    0,    0};
    u32 i;

    if (target.kind != CGF_TARGET_X86_64_LINUX_GNU &&
        target.kind != CGF_TARGET_X86_64_LINUX_MUSL &&
        target.kind != CGF_TARGET_X86_64_FREEBSD)
        CGF_ICE("x86-64 CFI encoder received non-x86 target %d",
                (int)target.kind);
    buf_printf(out, "\t.section\t.eh_frame,\"a\",@progbits\n"
                    "\t.p2align\t3\n"
                    ".Lcie0:\n"
                    "\t.long\t.Lcie_end-.Lcie_start\n"
                    ".Lcie_start:\n"
                    "\t.long\t0\n");
    emit_byte_list(out, cie, sizeof(cie));
    buf_printf(out, ".Lcie_end:\n");
    (void)m; /* FDEs name local labels now, not the function symbols */
    for (i = 0; i < nfuncs; i++) {
        buf_printf(out,
                   ".Lfde%u:\n"
                   "\t.long\t.Lfde%u_end-.Lfde%u_start\n"
                   ".Lfde%u_start:\n"
                   ".Lfde%u_cie:\n"
                   "\t.long\t.Lfde%u_cie-.Lcie0\n"
                   "\t.long\t.Lfb%u-.\n"
                   "\t.long\t.Lfe%u_0-.Lfb%u\n",
                   i, i, i, i, i, i, i, i, i);
        emit_byte_list(out, fde, sizeof(fde));
        buf_printf(out, ".Lfde%u_end:\n", i);
    }
}

static void verify_cfi_prologue(const X64Func *f)
{
    const X64Inst *push;
    const X64Inst *mov;

    if (!f->allocated || !f->nblocks || f->blocks[0].n < 2)
        CGF_ICE("CFI: function '%s' has no finalized rbp prologue", f->name);
    push = &f->blocks[0].insts[0];
    mov = &f->blocks[0].insts[1];
    if (push->op != X64_OP_PUSH || push->width != X64_Q ||
        push->a.kind != X64O_VREG || push->a.r.v != X64_RBP + 1 ||
        mov->op != X64_OP_MOV || mov->width != X64_Q ||
        mov->def.v != X64_RBP + 1 || mov->a.kind != X64O_VREG ||
        mov->a.r.v != X64_RSP + 1)
        CGF_ICE("CFI: function '%s' violates the push-rbp/mov-rsp-rbp law",
                f->name);
}

void x64_emit_debug_sections(TargetSpec target, Arena *arena, const IrModule *m,
                             X64Func *const *funcs, u32 nfuncs,
                             const char *input, const char *comp_dir,
                             bool emit_debug, Buf *out)
{
    CgDebugFunc *dfuncs;
    u32 fi, bi, i, n;

    for (fi = 0; fi < nfuncs; fi++)
        verify_cfi_prologue(funcs[fi]);
    emit_eh_frame(target, m, nfuncs, out);
    if (!emit_debug)
        return;

    /* Flatten each function's instruction stream into the (label, loc) rows
     * cg_emit_debug_info consumes. x64_debug_prepare already decided WHICH
     * instructions carry a label; this only copies those decisions out from
     * behind the X64 structures. */
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
            const X64Block *b = &funcs[fi]->blocks[bi];

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
